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
    serviceUUID: '19B10010-E8F2-537E-4F6C-D104768A1214',
    buttonCharacteristic: '19B10011-E8F2-537E-4F6C-D104768A1214', // transmit is from the phone's perspective
};

var app =
{
    initialize: function()
    {
        setTimeout(() => {  $("#index").remove()}, 2000);
        document.addEventListener("deviceReady", app.onDeviceReady, false);
        $("#refreshButton").on("click", app.disconnect);
        $("#refreshButton").on("click", app.refreshDeviceList);
        $("#openButton").on("click", app.open);
        $("#closeButton").on("click", app.close);
        $("#disconnectButton").on("click", app.disconnect);
        document.getElementById("command-panel").style.display = "none";
        PullToRefresh.init({
            mainElement: 'body',
            onRefresh: function(){
                // if no ble peripheral connected, refresh devices list
                if(!$("#disconnectButton").data("deviceId")) {
                    app.refreshDeviceList();
                }
            }
        });
    },

    onDeviceReady: function()
    {
        app.refreshDeviceList();
    },

    refreshDeviceList: function()
    {
        $("#refreshButton").addClass("btnclick");
        $("#deviceList").html(""); // empties the list
        ble.scan([cesam.serviceUUID], 5, app.onDiscoverDevice, app.onError);
    },

    onDiscoverDevice: function(device)
    {
        if(device.name == "CESAM")
        {
            var listItem = $("<li/>").data("deviceId", device.id)
                                     .html(device.name + "&nbsp;" +
                                        "(RSSI: " + device.rssi + "dBm&nbsp;|&nbsp;" +
                                        device.id + ")")
                                     .on("click", app.connect);
            $("#deviceList").append(listItem);
        }
    },

    connect: function(e)
    {
        var target = $(e.target)
        var deviceId = $(e.target).data("deviceId")

        target.addClass("connection")

        function onConnect(peripheral)
        {
            //app.determineWriteType(peripheral);


            // subscribe for incoming data
            //ble.startNotification(deviceId, cesam.serviceUUID, cesam.buttonCharacteristic, app.onData, app.onError);

            $("#disconnectButton").data("deviceId", deviceId);
            target.removeClass("connection");
            target.addClass("connected");
            document.getElementById("command-panel").style.display = "block";
        };

        // If not already connected, connect to the selected device
        if(!$("#disconnectButton").data("deviceId"))
        {
            ble.connect(deviceId, onConnect, app.onError);
        }
    },

    determineWriteType: function(peripheral)
    {
        var characteristic = peripheral.characteristics.filter(function(element)
        {
            if(element.characteristic.toLowerCase() === cesam.buttonCharacteristic)
            {
                return element;
            }
        })[0];

        app.writeWithoutResponse = (characteristic.properties.indexOf('WriteWithoutResponse') > -1);
    },

    sendData: function(data)
    {
        var deviceId = $("#disconnectButton").data("deviceId");

        function success()
        {
        };

        function failure(reason)
        {
            alert("Failed writing data to CESAM " + JSON.stringify(reason));
        };

        if(deviceId)
        {
            if(app.writeWithoutResponse)
            {
                ble.writeWithoutResponse(
                    deviceId,
                    cesam.serviceUUID,
                    cesam.buttonCharacteristic,
                    stringToBytes(data), success, failure
                );
            }
            else
            {
                ble.write(
                    deviceId,
                    cesam.serviceUUID,
                    cesam.buttonCharacteristic,
                    stringToBytes(data), success, failure
                );
            }
        }
    },

    open: function(event)
    {
        $("#openButton").addClass("btnclick");
        app.sendData("0");
    },

    close: function(event)
    {
        $("#closeButton").addClass("btnclick");
        app.sendData("1");
    },

    disconnect: function(event)
    {
        $("#disconnectButton").addClass("btnclick");
        var deviceId = $("#disconnectButton").data("deviceId");

        if(deviceId)
        {
            console.log(deviceId + ' disconnected');
            ble.disconnect(deviceId, app.disconnected, app.onError);
        }
    },

    disconnected: function()
    {
        $("#deviceList > li").removeClass("connected");
        $("#disconnectButton").data("deviceId", null);
        document.getElementById("command-panel").style.display = "none";
    },

    onError: function(reason)
    {
        alert("ERROR: " + JSON.stringify(reason)); // real apps should use notification.alert
        app.disconnect();
        app.disconnected();

    },
};

app.initialize();
