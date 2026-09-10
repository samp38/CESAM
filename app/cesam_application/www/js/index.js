'use strict';


// ASCII only
function bytesToString(buffer)
{
    return String.fromCharCode.apply(null, new Uint8Array(buffer));
}

// ASCII only
function stringToBytes(string)
{
    var array = new Uint8Array(string.length);

    for(var i = 0, l = string.length; i < l; i++)
    {
        array[i] = string.charCodeAt(i);
    }

    return array.buffer;
}

function bitLength(number) {
  return Math.floor(Math.log2(number)) + 1;
}

function byteLength(number) {
  return Math.ceil(bitLength(number) / 8);
}

function toBytes(number) {
  if (!Number.isSafeInteger(number)) {
    throw new Error("Number is out of range");
  }

  const size = number === 0 ? 0 : byteLength(number);
  const bytes = new Uint8ClampedArray(size);
  let x = number;
  for (let i = (size - 1); i >= 0; i--) {
    const rightByte = x & 0xff;
    bytes[i] = rightByte;
    x = Math.floor(x / 0x100);
  }

  return bytes.buffer;
}

function fromBytes(buffer) {
  const bytes = new Uint8ClampedArray(buffer);
  const size = bytes.byteLength;
  let x = 0;
  for (let i = 0; i < size; i++) {
    const byte = bytes[i];
    x *= 0x100;
    x += byte;
  }
  return x;
}


var cesam =
{
    serviceUUID: '6e400001-b5a3-f393-e0a9-e50e24dcca9e',
    buttonCharacteristic: '6e400002-b5a3-f393-e0a9-e50e24dcca9e',
    speedCharacteristic: '6e400003-b5a3-f393-e0a9-e50e24dcca9e',
    nameCharacteristic: '6e400004-b5a3-f393-e0a9-e50e24dcca9e',
    stateCharacteristic: '6e400005-b5a3-f393-e0a9-e50e24dcca9e',
    reverseCharacteristic: '6e400006-b5a3-f393-e0a9-e50e24dcca9e',

    scanSeconds: 5,
    maxNameLength: 29, // MAX_NAME_LEN in the firmware: what fits in the scan response

    // boards to connect to on startup, as { id: last known name }
    storageKey: "cesam.autoConnect",

    CMD_OPEN: "0",
    CMD_CLOSE: "1",
    CMD_REFRESH: "2",
    CMD_PAUSE: "3",

    // values of the state characteristic, a raw byte (DOOR_STATE_* in the firmware).
    // "Ouverte" and "Fermée" mean a travel ran to completion, not a measured position:
    // the board has no limit switch and infers the end of travel from the IMU.
    doorStates: {
        0: "Démarrage",
        1: "Position inconnue",
        2: "Ouverte",
        3: "Fermée",
        4: "En pause",
        5: "Ouverture…",
        6: "Fermeture…"
    },

    // speed is a single byte, and wraps around instead of saturating
    SPEED_MIN: 5,
    SPEED_MAX: 255,
    SPEED_STEP: 25
};


// Every discovered peripheral, keyed by its BLE id (a MAC address on Android).
// Several of them may be connected at the same time, so all per-device state -
// connection state and current speed included - lives here rather than in the DOM.
//
// {
//   id, name, rssi,
//   state:     "disconnected" | "connecting" | "connected",
//   doorState: the last state notified by the board, or null while unknown,
//   speed:     the last speed notified by the board, or null while unknown,
//   reversed:  true when the board inverts the motor direction; assumed false until the
//              board has been read, which matches the firmware default,
//   nameDraft: what the user is currently typing in the rename field, or null,
//   auto:      connect to this board on startup, remembered across app launches,
//   status:    a short message shown under the device name, or null
// }
var devices = {};


var app =
{
    initialize: function()
    {
        setTimeout(() => {  $("#index").remove()}, 2000);
        document.addEventListener("deviceready", app.onDeviceReady, false);

        // Handlers are delegated to #deviceList: device rows are re-rendered on every
        // state change, so binding directly on them would not survive a redraw.
        var list = $("#deviceList");
        list.on("click", ".device-header", function() {
            app.connect(app.deviceIdOf(this));
        });
        list.on("click", ".openButton", function() {
            app.sendCommand(app.deviceIdOf(this), cesam.CMD_OPEN);
        });
        list.on("click", ".closeButton", function() {
            app.sendCommand(app.deviceIdOf(this), cesam.CMD_CLOSE);
        });
        list.on("click", ".pauseButton", function() {
            app.sendCommand(app.deviceIdOf(this), cesam.CMD_PAUSE);
        });
        list.on("click", ".speedMinus", function() {
            app.incrementSpeed(app.deviceIdOf(this), -cesam.SPEED_STEP);
        });
        list.on("click", ".speedPlus", function() {
            app.incrementSpeed(app.deviceIdOf(this), cesam.SPEED_STEP);
        });
        list.on("click", ".reverseButton", function() {
            app.toggleReverse(app.deviceIdOf(this));
        });
        list.on("click", ".renameButton", function() {
            app.rename(app.deviceIdOf(this));
        });
        list.on("click", ".autoButton", function() {
            app.toggleAuto(app.deviceIdOf(this));
        });
        list.on("click", ".disconnectButton", function() {
            app.disconnect(app.deviceIdOf(this));
        });
        // kept out of the device state so a redraw does not wipe what is being typed
        list.on("input", ".nameInput", function() {
            var device = devices[app.deviceIdOf(this)];

            if(device)
            {
                device.nameDraft = $(this).val();
            }
        });

        PullToRefresh.init({
            mainElement: 'body',
            onRefresh: function(){
                // Rescan and, at the same time, ask every connected board for its
                // current parameters. Both are safe to do while connections are open.
                app.refreshDeviceList();
                app.connectedIds().forEach(app.refreshParameters);
            }
        });
    },

    onDeviceReady: function()
    {
        // Boards marked for auto-connect are listed straight away, before any scan, so
        // that one out of range still shows up - ble.autoConnect waits for it forever.
        var known = app.loadKnown();

        Object.keys(known).forEach(function(id) {
            devices[id] = app.newDevice(id, known[id]);
            devices[id].auto = true;
            app.connect(id);
        });

        app.refreshDeviceList();
    },

    // The set of boards to reconnect to, as { id: last known name }. localStorage throws
    // in a private window or with site data blocked, so no read or write may be assumed
    // to work.
    loadKnown: function()
    {
        try
        {
            return JSON.parse(localStorage.getItem(cesam.storageKey)) || {};
        }
        catch(e)
        {
            console.error("Could not read known devices: " + e);
            return {};
        }
    },

    saveKnown: function()
    {
        var known = {};

        Object.keys(devices).forEach(function(id) {
            if(devices[id].auto)
            {
                known[id] = devices[id].name;
            }
        });

        try
        {
            localStorage.setItem(cesam.storageKey, JSON.stringify(known));
        }
        catch(e)
        {
            console.error("Could not save known devices: " + e);
        }
    },

    toggleAuto: function(deviceId)
    {
        var device = devices[deviceId];

        if(!device)
        {
            return;
        }

        device.auto = !device.auto;
        app.saveKnown();

        // Turning it on acts right away rather than only at the next startup. Turning it
        // off leaves the connection alone: the board may well be in use.
        if(device.auto && device.state === "disconnected")
        {
            app.connect(deviceId); // renders
        }
        else
        {
            app.render();
        }
    },

    newDevice: function(id, name)
    {
        return {
            id: id,
            name: name || "CESAM",
            rssi: undefined,
            state: "disconnected",
            doorState: null,
            speed: null,
            reversed: false,
            nameDraft: null,
            auto: false,
            status: null
        };
    },

    deviceIdOf: function(element)
    {
        return $(element).closest("li.device").attr("data-id");
    },

    connectedIds: function()
    {
        return Object.keys(devices).filter(function(id) {
            return devices[id].state === "connected";
        });
    },

    refreshDeviceList: function()
    {
        // A connected peripheral is never reported again by ble.scan (the plugin only
        // drops non-connected entries from its cache), so connected devices have to be
        // kept in the list: dropping them here would lose the only handle we have on
        // them and leave the connection open with no way to close it. Boards marked for
        // auto-connect stay too, even out of range - they are the ones the user wants to
        // see waiting rather than see vanish.
        Object.keys(devices).forEach(function(id) {
            if(devices[id].state === "disconnected" && !devices[id].auto)
            {
                delete devices[id];
            }
        });
        app.render();

        function scan()
        {
            ble.scan([cesam.serviceUUID], cesam.scanSeconds, app.onDiscoverDevice,
                     function(reason) {
                         console.error("Scan failed: " + JSON.stringify(reason));
                         app.showListMessage("La recherche a échoué. Tirez vers le bas " +
                                             "pour réessayer.");
                     });
            // ble.scan has no "scan finished" callback, so tell the user about an empty
            // list once the scanning seconds have elapsed
            setTimeout(function() {
                if(Object.keys(devices).length === 0)
                {
                    app.showListMessage("Aucun CESAM trouvé. Vérifiez qu'il est allumé, " +
                                        "puis tirez vers le bas pour relancer la recherche.");
                }
            }, cesam.scanSeconds * 1000 + 500);
        }

        ble.isEnabled(scan, function() {
            app.showListMessage("Bluetooth désactivé. Activez-le, puis tirez vers le bas " +
                                "pour relancer la recherche.");
        });
    },

    // ble.scan already filters on the service UUID, so everything reported here is a
    // CESAM. Filtering on the name again would break as soon as a board is renamed.
    onDiscoverDevice: function(device)
    {
        if(!devices[device.id])
        {
            devices[device.id] = app.newDevice(device.id, device.name);
        }

        devices[device.id].rssi = device.rssi;
        devices[device.id].name = device.name || devices[device.id].name;
        app.render();
    },

    connect: function(deviceId)
    {
        var device = devices[deviceId];

        // Only connect an idle device. Marking it "connecting" right now - and not in
        // the connect callback, which lands a second or two later - is what keeps a
        // double tap from opening two connections to the same board.
        if(!device || device.state !== "disconnected")
        {
            return;
        }

        device.state = "connecting";
        device.status = null;
        app.render();

        function onConnect(peripheral)
        {
            device.state = "connected";
            device.status = null;
            app.render();

            // subscribe for incoming data from speed
            ble.startNotification(deviceId, cesam.serviceUUID, cesam.speedCharacteristic,
                function(data) {
                    app.onSpeedData(deviceId, data);
                },
                function(reason) {
                    console.error("Speed notifications failed on " + deviceId + ": " +
                                  JSON.stringify(reason));
                    device.status = "Vitesse non disponible";
                    app.render();
                });

            // and from the door state
            ble.startNotification(deviceId, cesam.serviceUUID, cesam.stateCharacteristic,
                function(data) {
                    app.onStateData(deviceId, data);
                },
                function(reason) {
                    console.error("State notifications failed on " + deviceId + ": " +
                                  JSON.stringify(reason));
                    device.status = "État non disponible";
                    app.render();
                });

            // the advertised name may be stale if the board was renamed from another
            // phone, so take it from the board itself
            ble.read(deviceId, cesam.serviceUUID, cesam.nameCharacteristic,
                function(data) {
                    device.name = bytesToString(data);
                    app.saveKnown();
                    app.render();
                },
                function(reason) {
                    console.error("Name read failed on " + deviceId + ": " +
                                  JSON.stringify(reason));
                });

            // motor wiring is a property of the installation, so it is stored on the
            // board and only known once read
            ble.read(deviceId, cesam.serviceUUID, cesam.reverseCharacteristic,
                function(data) {
                    device.reversed = fromBytes(data) !== 0;
                    app.render();
                },
                function(reason) {
                    console.error("Reverse read failed on " + deviceId + ": " +
                                  JSON.stringify(reason));
                });

            app.refreshParameters(deviceId);
        }

        // The third callback is not just a failure callback: the plugin also calls it
        // later on, when the peripheral itself drops the connection. Either way it
        // concerns this device only - it must never tear down the other connections.
        // It is not called when the app is the one disconnecting, so reaching it always
        // means the board went away and autoConnect is now waiting for it to come back.
        function onDisconnect(reason)
        {
            console.log("Disconnected from " + deviceId + ": " + JSON.stringify(reason));
            device.status = "Connexion perdue, reconnexion…";
            app.disconnected(deviceId);
        }

        // autoConnect rather than connect: it never times out, waits for the board to be
        // in range, and re-establishes the link on its own every time the board goes away
        // and comes back. Pressing Déconnecter is what stops it. The auto flag is about
        // something else - whether to do this at startup without being asked.
        ble.autoConnect(deviceId, onConnect, onDisconnect);
    },

    disconnect: function(deviceId)
    {
        var device = devices[deviceId];

        if(!device || device.state === "disconnected")
        {
            return;
        }

        // The plugin does not call the disconnect callback when the app is the one
        // closing the connection, so the state is updated here. Disconnecting also stops
        // the automatic reconnection, which the flag alone would not make obvious: it
        // says the board comes back at the next startup, not that something is retrying.
        if(device.auto)
        {
            device.status = "Reconnexion au prochain démarrage";
        }

        ble.disconnect(deviceId,
            function() {
                app.disconnected(deviceId);
            },
            function(reason) {
                console.error("Disconnect failed on " + deviceId + ": " +
                              JSON.stringify(reason));
                app.disconnected(deviceId);
            });
    },

    disconnected: function(deviceId)
    {
        var device = devices[deviceId];

        if(!device)
        {
            return;
        }

        device.state = "disconnected";
        device.speed = null;
        device.doorState = null;
        device.reversed = false;
        device.nameDraft = null;
        app.render();
    },

    // Update a single field of one device rather than redrawing. A notification can land
    // while the user is pressing a button or typing a name, and a full redraw would
    // rebuild the element under their finger - swallowing the tap, or losing the text.
    updateField: function(deviceId, fieldClass, text)
    {
        var field = $("#deviceList > li.device[data-id='" + deviceId + "'] ." + fieldClass);

        if(field.length)
        {
            field.text(text);
        }
        else
        {
            app.render();
        }
    },

    onSpeedData: function(deviceId, data)
    {
        var device = devices[deviceId];

        if(!device)
        {
            return;
        }

        device.speed = fromBytes(data);
        console.log("Speed received from " + deviceId + " : " + device.speed);
        app.updateField(deviceId, "deviceSpeed", device.speed);
    },

    onStateData: function(deviceId, data)
    {
        var device = devices[deviceId];

        if(!device)
        {
            return;
        }

        device.doorState = fromBytes(data);
        console.log("State received from " + deviceId + " : " + device.doorState);
        app.updateField(deviceId, "deviceState", app.doorStateLabel(device));
    },

    doorStateLabel: function(device)
    {
        if(device.doorState === null)
        {
            return "NC";
        }

        // an unknown value means the board runs a firmware this app does not know about
        return cesam.doorStates[device.doorState] || ("Inconnu (" + device.doorState + ")");
    },

    // Bistable: the button shows the setting currently stored on the board, and one tap
    // flips it. The board echoes back the normalised value, which is what gets displayed.
    toggleReverse: function(deviceId)
    {
        var device = devices[deviceId];

        if(!device || device.state !== "connected")
        {
            return;
        }

        var wanted = device.reversed ? 0 : 1;

        // not toBytes(): it encodes 0 as an empty buffer, and the board expects one byte
        ble.write(deviceId, cesam.serviceUUID, cesam.reverseCharacteristic,
            new Uint8Array([wanted]).buffer,
            function() {
                ble.read(deviceId, cesam.serviceUUID, cesam.reverseCharacteristic,
                    function(data) {
                        device.reversed = fromBytes(data) !== 0;
                        app.render();
                    },
                    function() {
                        // the write went through, so trust it rather than showing nothing
                        device.reversed = (wanted === 1);
                        app.render();
                    });
            },
            function(reason) {
                console.error("Reverse write failed on " + deviceId + ": " +
                              JSON.stringify(reason));
                device.status = "Sens moteur non transmis";
                app.render();
            });
    },

    rename: function(deviceId)
    {
        var device = devices[deviceId];

        if(!device || device.state !== "connected" || device.nameDraft === null)
        {
            return;
        }

        var newName = device.nameDraft.trim();

        if(newName.length === 0 || newName === device.name)
        {
            device.nameDraft = null;
            app.render();
            return;
        }

        ble.write(deviceId, cesam.serviceUUID, cesam.nameCharacteristic,
            stringToBytes(newName.slice(0, cesam.maxNameLength)),
            function() {
                // read back what the board actually stored rather than assuming
                ble.read(deviceId, cesam.serviceUUID, cesam.nameCharacteristic,
                    function(data) {
                        device.name = bytesToString(data);
                        device.nameDraft = null;
                        app.saveKnown();
                        app.render();
                    },
                    function() {
                        device.nameDraft = null;
                        app.render();
                    });
            },
            function(reason) {
                console.error("Rename failed on " + deviceId + ": " + JSON.stringify(reason));
                device.status = "Renommage impossible";
                app.render();
            });
    },

    sendCommand: function(deviceId, command)
    {
        var device = devices[deviceId];

        if(!device || device.state !== "connected")
        {
            return;
        }

        ble.write(deviceId, cesam.serviceUUID, cesam.buttonCharacteristic,
            stringToBytes(command),
            function() {
                // a command got through, so any previous error message is stale
                if(device.status)
                {
                    device.status = null;
                    app.render();
                }
            },
            function(reason) {
                console.error("Write failed on " + deviceId + ": " + JSON.stringify(reason));
                device.status = "Commande non transmise";
                app.render();
            });
    },

    // Ask the board to send its parameters back over the speed notification
    refreshParameters: function(deviceId)
    {
        app.sendCommand(deviceId, cesam.CMD_REFRESH);
    },

    incrementSpeed: function(deviceId, incr)
    {
        var device = devices[deviceId];

        if(!device || device.state !== "connected")
        {
            return;
        }

        // Taken from this device's own state: reading it back from the DOM would pick
        // up whichever board notified last.
        var speed = (device.speed === null) ? cesam.SPEED_MAX : device.speed;
        var newspeed = speed + incr;

        if((incr < 0) && (speed < -incr + 1))
        {
            newspeed = cesam.SPEED_MAX;
        }
        else if((incr > 0) && (speed > cesam.SPEED_MAX - incr))
        {
            newspeed = cesam.SPEED_MIN;
        }

        ble.write(deviceId, cesam.serviceUUID, cesam.speedCharacteristic,
            toBytes(newspeed),
            function() {
                // Ask hardware to send updated values
                app.refreshParameters(deviceId);
            },
            function(reason) {
                console.error("Speed write failed on " + deviceId + ": " +
                              JSON.stringify(reason));
                device.status = "Vitesse non transmise";
                app.render();
            });
    },

    showListMessage: function(message)
    {
        if(Object.keys(devices).length === 0)
        {
            $("#deviceList").html($("<li/>").addClass("empty").text(message));
        }
    },

    render: function()
    {
        var list = $("#deviceList");
        var ids = Object.keys(devices);

        list.empty();

        ids.forEach(function(id) {
            list.append(app.renderDevice(devices[id]));
        });
    },

    renderDevice: function(device)
    {
        var item = $("<li/>").addClass("device").attr("data-id", device.id);
        var header = $("<div/>").addClass("device-header").appendTo(item);

        // Boards all advertise the same name, so the id is what tells them apart
        $("<span/>").addClass("device-name")
                    .text(device.name + " (" + device.id + ")")
                    .appendTo(header);

        var status;

        if(device.state === "connecting")
        {
            // autoConnect never times out, so a board that has not been seen in a scan
            // this session may sit here indefinitely: say so rather than imply progress
            status = (device.rssi === undefined) ? "En attente de la carte…" : "Connexion…";
        }
        else if(device.state === "connected")
        {
            status = device.status || "Connecté";
        }
        else
        {
            // keep the row advertised as tappable even when it carries an error
            status = device.status ? device.status + " · Appuyer pour reconnecter"
                                   : "Appuyer pour connecter";

            if(device.rssi !== undefined)
            {
                status += " · RSSI " + device.rssi + " dBm";
            }
        }

        $("<span/>").addClass("device-status")
                    .addClass(device.state === "connected" ? "connected" :
                              device.state === "connecting" ? "connection" : "")
                    .text(status)
                    .appendTo(header);

        // Outside .device-header, whose click connects, and outside the command panel,
        // which only exists while connected: a waiting board must show its flag too, and
        // be able to have it cleared.
        var autoRow = $("<div/>").addClass("row auto-row");
        $("<span/>").addClass("label").text("Connexion auto : ").appendTo(autoRow);
        $("<button/>").addClass("autoButton")
                      .toggleClass("on", device.auto)
                      .attr("aria-pressed", device.auto ? "true" : "false")
                      .text(device.auto ? "Oui" : "Non")
                      .appendTo(autoRow);
        item.append(autoRow);

        if(device.state === "connected")
        {
            item.append(app.renderCommandPanel(device));
        }

        return item;
    },

    renderCommandPanel: function(device)
    {
        var panel = $("<div/>").addClass("command device-panel");

        $("<button/>").addClass("openButton").text("Ouvrir").appendTo(panel);
        $("<button/>").addClass("closeButton").text("Fermer").appendTo(panel);
        $("<button/>").addClass("pauseButton").text("Pause").appendTo(panel);

        var stateRow = $("<div/>").addClass("row").appendTo(panel);
        $("<span/>").addClass("label").text("État : ").appendTo(stateRow);
        $("<span/>").addClass("deviceState")
                    .text(app.doorStateLabel(device))
                    .appendTo(stateRow);

        var speedRow = $("<div/>").addClass("row").appendTo(panel);
        $("<span/>").addClass("label").text("Vitesse : ").appendTo(speedRow);
        $("<span/>").addClass("deviceSpeed")
                    .text(device.speed === null ? "NC" : device.speed)
                    .appendTo(speedRow);
        $("<button/>").addClass("speedMinus settingButton").text("-").appendTo(speedRow);
        $("<button/>").addClass("speedPlus settingButton").text("+").appendTo(speedRow);

        var reverseRow = $("<div/>").addClass("row").appendTo(panel);
        $("<span/>").addClass("label").text("Sens moteur : ").appendTo(reverseRow);
        $("<button/>").addClass("reverseButton")
                      .toggleClass("on", device.reversed)
                      .attr("aria-pressed", device.reversed ? "true" : "false")
                      .text(device.reversed ? "Inversé" : "Normal")
                      .appendTo(reverseRow);

        var nameRow = $("<div/>").addClass("row").appendTo(panel);
        $("<span/>").addClass("label").text("Nom : ").appendTo(nameRow);
        $("<input/>").addClass("nameInput")
                     .attr("type", "text")
                     .attr("maxlength", cesam.maxNameLength)
                     .val(device.nameDraft === null ? device.name : device.nameDraft)
                     .appendTo(nameRow);
        $("<button/>").addClass("renameButton settingButton").text("OK").appendTo(nameRow);

        $("<button/>").addClass("disconnectButton").text("Déconnecter").appendTo(panel);

        return panel;
    }
};

app.initialize();
