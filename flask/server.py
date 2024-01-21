from flask import Flask, request
from flask_socketio import SocketIO, emit
import yaml
import json
import os
import mysql.connector
import requests
import traceback
import schedule
from threading import Thread
from time import sleep
from math import sqrt, e, pi, pow


app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*")

network = {}
parking_lots = {}
ips = {}
dir_path = os.path.dirname(os.path.realpath(__file__))
with open(f"{dir_path}/config.yml", 'r') as stream:
    try:
        config = yaml.safe_load(stream)
    except yaml.YAMLError as exc:
        print(exc)


# new data coming from master
@app.route('/parking/<parking>', methods = ['POST'])
def parking(parking):
    network[parking] = json.loads(request.data)
    socketio.emit(f'{parking}_parking', {'network': network[parking]}, broadcast=True)
    socketio.emit(f'{parking}_info', {'info': parking_lots[parking]}, broadcast=True)
    return "OK"

# booking
@app.route('/parking/<parking>/book', methods = ['POST'])
def book(parking):
    for mac in network[parking]:
        if network[parking][mac]['status'] == 'free' and network[parking][mac]['booked'] == 'false' and network[parking][mac]['online']=='true':
            body = {'mac': mac,
                    'book': request.json['book']}
            requests.post('http://'+ips[parking]+'/book', json=body)
            return '', 200
        else:
            continue
    return '', 400

# unbooking
@app.route('/parking/<parking>/unbook', methods = ['POST'])
def unbook(parking):
    for mac in network[parking]:
        if network[parking][mac]['booked'] == request.json['book']:     # se c'è un posto prenotato dall'utente
            body = {'mac': mac,
                    'book': "false"}
            requests.post('http://'+ips[parking]+'/book', json=body)
            return '', 200
        else:
            continue
    return '', 400


@app.route('/bookings/<user>', methods = ['GET'])
def get_bookings(user):
    bookings = []
    for parking_lot in network:
        for mac in network[parking_lot]:
            if network[parking_lot][mac]['booked'] == user:
                bookings.append(parking_lot)
                break
    return {'bookings': bookings}


# get parkings
@app.route('/', methods=['GET'])
def get_parkings():
    data = {}

    for parking in parking_lots.keys():
        if parking_lots[parking]['closed'] == 0 and not parking_lots[parking]['offline']:
            if parking in network.keys():
                count = 0
                for mac in network[parking]:
                    if network[parking][mac]['status'] == 'free' and network[parking][mac]['booked'] == 'false'and network[parking][mac]['online']=='true':
                        count += 1

                data[parking] = {'id': parking_lots[parking]['id'],
                                'name': parking_lots[parking]['name'],
                                'free_parkings': count}

    return data


@socketio.on('getData')
def get_data(target):
    if target == 'home':
        socketio.emit(f'info', {'info': parking_lots}, broadcast=True)
    else:
        socketio.emit(f'{target}_parking', {'network': network[target]}, broadcast=True)
        socketio.emit(f'{target}_info', {'info': parking_lots[target]}, broadcast=True)

@socketio.on('connect')
def connect():
    print('Client connected')
    emit('my response', {'data': 'Connected'})

@socketio.on('disconnect')
def disconnect():
    print('Client disconnected')


def run_threaded(job_func, *args, **kwargs):
    job_thread = Thread(target=job_func, args=args, kwargs=kwargs)
    job_thread.start()


def update_parking_lots_schedule():
    schedule.every(10).seconds.do(update_parking_lots)
    while True:
        schedule.run_pending()
        sleep(1)


def update_parking_lots():
    print('Updating parking lots...')

    db = mysql.connector.connect(
        host=config['DB_HOST'],
        user=config['DB_USER'],
        password=config['DB_PASSWORD']
    )
    cursor = db.cursor()
    cursor.execute("SELECT * FROM ddovico.parking;")

    row_headers=[x[0] for x in cursor.description]

    for result in cursor:
        p = dict(zip(row_headers,result))
        id = p['id']
        ip = p['ipAddress']
        p.pop('ipAddress', None)
        parking_lots.update({id: p})
        ips.update({id: ip})

    db.close()

    for id, ip in ips.items():
        try:
            data = requests.get('http://'+ip, timeout=4).content
            data = json.loads(data)
            network[id]=data
            parking_lots[id]['offline'] = False
        except Exception:
            traceback.print_exc()
            parking_lots[id]['offline'] = True
    
    socketio.emit(f'info', {'info': parking_lots}, broadcast=True)



@app.route('/parking/<parking>/threshold', methods = ['POST'])
def threshold(parking):
    data = json.loads(request.data)

    min_threshold = data['min_threshold']
    max_threshold = data['max_threshold']

    token = config['OWM_TOKEN']
    position = parking_lots[parking]['position']

    position = position.split(', ')
    lat = position[0]
    lon = position[1]

    url = f'https://api.openweathermap.org/data/2.5/weather?lat={lat}&lon={lon}&appid={token}'
    data = requests.get(url).json()

    sunrise = data['sys']['sunrise']
    sunset = data['sys']['sunset']
    current = data['dt']

    delta_sun = (current - sunrise)/(sunset - sunrise)

    cloud = data['clouds']['all']/100

    thr = calculate_threshold(min_threshold, max_threshold, delta_sun, cloud)

    return str(thr)


def open_close(parking, closed):
    body = {
        'close': closed
    }
    status_code = requests.post('http://'+ips[parking]+'/close', json=body).status_code

    parking_lots[parking]['closed'] = closed
    socketio.emit(f'{parking}_info', {'info': parking_lots[parking]}, broadcast=True)
    return '', status_code


@socketio.on('close')
def close(target):
    open_close(target, True)


@socketio.on('open')
def open(target):
    open_close(target, False)
    


def calculate_threshold(min_threshold, max_threshold, delta_sun, cloud):
    sun_level_exp = -2 * pow(2 * delta_sun -1, 2)
    sun_level = 5 * pow(e, sun_level_exp) / (2 * sqrt(2 * pi))

    sun_level_with_clouds = sun_level * (1 - cloud * 0.3)

    result = round(sun_level_with_clouds * (max_threshold-min_threshold) + min_threshold)

    return result




if __name__ == '__main__':
    update_parking_lots()
    run_threaded(update_parking_lots_schedule)
    socketio.run(app, debug=True, host='0.0.0.0', port=5000, use_reloader=False)
    