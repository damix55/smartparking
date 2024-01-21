import React, {Component} from 'react'
import { compose, withProps } from "recompose";
import socketIOClient from 'socket.io-client';
import {
  withScriptjs,
  withGoogleMap,
  GoogleMap,
  Marker
} from "react-google-maps";
import {flaskUrl} from './config.js';

const socket = socketIOClient(flaskUrl)

const MyMapComponent = compose(
  withProps({
    googleMapURL:
      "https://maps.googleapis.com/maps/api/js?key=AIzaSyBsU2L9MY9ulSia9t7Wu_6t80-KNYWrUwo&v=3.exp&libraries=geometry,drawing,places",
    loadingElement: <div style={{ height: `100%` }} />,
    containerElement: <div style={{ height: `700px`, paddingTop: `16px` }} />,
    mapElement: <div style={{ height: `100%` }} />
  }),
  withScriptjs,
  withGoogleMap
)(props => (
  <GoogleMap defaultZoom={11} defaultCenter={{ lat: 45.642942, lng: 9.326865 }}>
    {props.parkings.map((parking, index) => 
      <Marker
        position = {{ lat: parking.lat, lng: parking.lng }}
        label = { parking.name.substring(0,1) }
      />
    )}
  </GoogleMap>
));

export default class App extends Component {

  constructor(props){
    super(props);
    this.state = {
        parkings : []
    }
  };

  componentDidMount() {
    document.title = "SmartParking";
    socket.emit("getData", "home")
    socket.on("info", (data) => {
      data = Object.values(data.info)
      
      for (let i = 0; i < data.length; ++i) {
        let position = data[i].position.split(", ")
        data[i].lat = parseFloat(position[0])
        data[i].lng = parseFloat(position[1])
      }
      this.setState({parkings: data});
    });
  }

  render(){
    return(
    <div>
      <nav class="navbar navbar-dark bg-dark">
        <div class="container">
          <a class="navbar-brand p-2" href="/">SmartParking</a>
          <a class="nav-link active text-white" href="http://t.me/parking_iot_bot">Prenota un posto</a>
        </div>
      </nav>
      <div class="container">
        <div class="row">
          <div class="col-8">
            <MyMapComponent
              parkings={this.state.parkings}
            />
          </div>
          <div class="col-4">
            {this.state.parkings.map((parking, index) => 
            ! parking.offline
            ? <a href={"/parking/" + parking.id} key={index} class="card m-3">
                <img src={"../img/" + parking.id + ".jpg"} class="card-img-top" style={{ height: `150px`, objectFit: `cover` }} />
                <div class="card-body">
                  <div class="row">
                    <div class="col">
                      <h5 class="card-title">{parking.name}
                      </h5>
                    </div>
                    <div class="col-auto">
                      {parking.closed 
                        ? <span class="dot-red"></span>
                        : <span class="dot-green"></span>
                      }
                    </div>
                  </div>
                  <p class="card-text">{parking.address}</p>
                </div>
              </a>
            : <div key={index} class="card m-3">
                <img src={"../img/" + parking.id + ".jpg"} class="card-img-top" style={{ height: `150px`, objectFit: `cover`, opacity: '60%' }} />
                <div class="card-body">
                  <div class="row">
                    <div class="col">
                      <h5 class="card-title text-secondary">{parking.name}
                      </h5>
                    </div>
                    <div class="col-auto">
                    </div>
                  </div>
                  <p class="card-text text-secondary">{parking.address}</p>
                </div>
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
    );
  }
}



