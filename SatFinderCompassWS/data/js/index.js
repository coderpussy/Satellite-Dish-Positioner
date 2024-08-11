// Initialization
var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
window.addEventListener('load', onload);
var action;

var oc_azimut = document.getElementById("oc_azimut");
var oc_elevation = document.getElementById("oc_elevation");
var oc_el_offset = document.getElementById("oc_el_offset");
var oc_az_offset = document.getElementById("oc_az_offset");
var oc_motor_speed = document.getElementById("oc_motor_speed");

var om_time = document.getElementById("om_time");
var om_speed = document.getElementById("om_speed");
var om_steps = document.getElementById("om_steps");
var om_azimut = document.getElementById("om_azimut");
var om_elevation = document.getElementById("om_elevation");

var led_level = document.getElementById("led_level");
var state = document.getElementById("state");
var azimut = document.getElementById("azimut");
var elevation = document.getElementById("elevation");

var s_azimut = document.getElementById("s_azimut");
var s_elevation = document.getElementById("s_elevation");

var slider1 = document.getElementById("rotor_range");
var output1 = document.getElementById("rotor");

var slider2 = document.getElementById("d_azimut_range");
var output2 = document.getElementById("d_azimut");

var slider3 = document.getElementById("d_elevation_range");
var output3 = document.getElementById("d_elevation");

// Slider on change function
function initSliderChange() {
    document.querySelectorAll('input[type="range"]').forEach(function (slider) {
        slider.addEventListener('change', function () {
            let output = '#'+slider.id.substring(0, slider.id.lastIndexOf('_'));
            document.querySelector(output).innerHTML = (slider.value *1).toFixed(1);

            websocket.send(JSON.stringify({"action":slider.id,"level":slider.value}));
        });
    });
}

// Button on click function with event listener
function initButtonClick() {
    document.querySelectorAll('button').forEach(function (btn) {
        btn.addEventListener('click', function () {
            action = btn.id;

            if (action == "savesettings" || action == "backsettings") {
                if (action == "savesettings") {
                    saveSettings();
                } else {
                    toggleOverlay("settings");
                }
            }
            else if (action == "om_el_up" || action == "om_el_down") {
                websocket.send(JSON.stringify({"time":om_time.value,"speed":om_speed.value,"action":action}));
            }
            else if (action == "om_az_up" || action == "om_az_down") {
                if (action == "om_az_up") {
                    if (om_steps.value == "short") {
                        websocket.send(JSON.stringify({"action":"rotor_up_step"}));
                    } else {
                        websocket.send(JSON.stringify({"action":"rotor_up"}));
                    }
                } else {
                    if (om_steps.value == "short") {
                        websocket.send(JSON.stringify({"action":"rotor_down_step"}));
                    } else {
                        websocket.send(JSON.stringify({"action":"rotor_down"}));
                    }
                }
            }
            else if (action == "om_stop") {
                return null;
            } else {
                websocket.send(JSON.stringify({"action":action}));
            }
        });
    });
}

// Init overlay toggle with event listener
function initToggleOverlay() {
    const overlaymanual = document.querySelector('#overlay-manual');
    const overlaysettings = document.querySelector('#overlay-settings');

    overlaymanual.addEventListener('click', e => { e.target.id === overlaymanual.id ? toggleOverlay("manual"):null });
    overlaysettings.addEventListener('click', e => { e.target.id === overlaysettings.id ? toggleOverlay("settings"):null });

    document.querySelectorAll('.toggle-overlay').forEach(function (elem) {
        //console.log(elem);
        elem.addEventListener('click', function () {
            if (elem.classList.contains("manual")) {
                toggleOverlay("manual");
            }
            if (elem.classList.contains("settings")) {
                toggleOverlay("settings");
            }
        });
    });
}

function initWebSocket() {
    console.log('Versuche WebSocket Verbindung herzustellen…');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

function onload(event) {
    initWebSocket();
    initSliderChange();
    initButtonClick();
    initToggleOverlay();
}

function onOpen(event) {
    console.log('Verbindung hergestellt');
    // get initial values data
    getValues();
    // get initial settings data
    getSettings();
}

function onClose(event) {
    console.log('Verbindung geschlossen');
    setTimeout(initWebSocket, 2000);
}

function onMessage(event) {
    let data = JSON.parse(event.data);

    action = data.action;
    console.log('message:',data);
    
    if (action == "getsettings" || action == "savesettings") {
        oc_azimut.value = data.azimut;
        oc_elevation.value = data.elevation;
        oc_el_offset.value = data.el_offset;
        oc_az_offset.value = data.az_offset;
        oc_motor_speed.value = data.motor_speed;
    } else {
        led_level.value = (data.led_level *1).toFixed(1);
        state.innerText = data.state;
        
        azimut.innerText = (data.azimut *1).toFixed(1);
        elevation.innerText = (data.elevation *1).toFixed(1);
        om_azimut.innerText = (data.azimut *1).toFixed(1);
        om_elevation.innerText = (data.elevation *1).toFixed(1);
    
        s_azimut.innerText = (data.s_azimut *1).toFixed(1);
        s_elevation.innerText = (data.s_elevation *1).toFixed(1);
            
        output1.innerHTML = (data.rotor*1).toFixed(1);
        slider1.value = data.rotor;

        output2.innerHTML = (data.d_azimut*1).toFixed(1);
        slider2.value = data.d_azimut;
        
        output3.innerHTML = (data.d_elevation*1).toFixed(1);
        slider3.value = data.d_elevation;
    }
}

function getValues() {
    websocket.send(JSON.stringify({"action":"getvalues"}));
}

function getSettings() {
    websocket.send(JSON.stringify({"action":"getsettings"}));
}

function saveSettings() {    
    websocket.send(JSON.stringify({"azimut":oc_azimut.value,"elevation":oc_elevation.value,"az_offset":oc_az_offset.value,"el_offset":oc_el_offset.value,"motor_speed":oc_motor_speed.value,"action":"savesettings"}));

    toggleOverlay("settings");
}

function toggleOverlay(mode) {
    document.getElementById("overlay-" + mode).classList.toggle("active");
}