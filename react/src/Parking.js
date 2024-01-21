import React, {Component} from 'react'
import socketIOClient from 'socket.io-client';
import {flaskUrl} from './config.js';

const socket = socketIOClient(flaskUrl)

export default class Parking extends Component {

  constructor(props){
    super(props);
    this.state = {
        parking : [],
        info: {}
    }
    this.id = this.props.match.params.parkingId
  };

  componentDidMount() {
    document.title = "SmartParking";
    socket.on(this.id + "_parking", (data) => {
      this.setState({parking: data.network});
    });
    socket.on(this.id + "_info", (data) => {
      this.setState({info: data.info});
    });
    socket.emit("getData", this.id)
  }

  handleClosed(closed){
    if (closed){
      socket.emit("close", this.id);
    } else {
      socket.emit("open", this.id);
    }
  }

  render() {
    return (
      <div>
        <nav class="navbar navbar-dark bg-dark">
          <div class="container">
            <a class="navbar-brand p-2" href="/">SmartParking</a>
            <a class="nav-link active text-white" href="http://t.me/parking_iot_bot">Prenota un posto</a>
          </div>
        </nav>
        <img src={"../img/" + this.state.info.id + ".jpg"} style={{ width: `100%`, height: `400px`, objectFit: `cover` }} />
        <div class="container">
          <div class="row">
            <div class="col-8">
              <h1 class="mt-3">{this.state.info.name}</h1>
              <h6 class="text-muted">{this.state.info.address}</h6>
            </div>
            <div
              class="col-4 mt-5"
              style={{
                textAlign: 'right'
              }}>
              {!this.state.info.closed
                  ? <button class="btn btn-primary" type="submit" onClick={() => this.handleClosed(true)}>Chiudi parcheggio</button>
                  : <button class="btn btn-danger" type="submit" onClick={() => this.handleClosed(false)}>Apri parcheggio</button>
              }
            </div>
          </div>
          <div
            class="row"
            style={{
              display: this.state.info.closed ? 'none' : 'flex'
          }}>
          {Object.values(this.state.parking).map((p, index) => 
            <div class="col-4">
              <div
                key={index}
                class="card mt-3"
              >
                <div class= {p.online === "false" ? "card-body" + " bg-light" : "card-body"}>
                  <div class="row">
                    <div class="col">
                      <h5 class={p.online === "false" ? "card-title" + " text-secondary" : "card-title"}>Posto {index + 1}
                      </h5>
                    </div>
                    <div class="col-auto">
                      {p.online==="true"
                      ? p.status!="occupied" && p.booked==="false"
                        ? <span class="dot-green"></span>
                        : <span class="dot-red"></span>
                      : null
                      }
                    </div>
                  </div>
                  <div class="row mt-3">
                    <div class="col mt-2 mb-0">
                      {p.online==="true"
                      ? p.booked!="false"
                        ? <p>Prenotato</p>
                        : p.status==="occupied"
                          ? <p>Occupato</p>
                          : <p>Disponibile</p>
                      : <p class={p.online === "false" ? "text-secondary" : ""}>Offline</p>
                      }
                    </div>
                  </div>
                </div>
              </div>
            </div>
          )}
          </div>
        </div>
      </div>
    );
  }
}
