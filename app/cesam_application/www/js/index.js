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

var exofinger =
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
        $("#openButtondebug").on("click", app.opendebug);
        $("#closeButtondebug").on("click", app.closedebug);
        $("#clearButtondebug").on("click", app.cleardebug);
        $("#courseButton").on("click", app.setCourse);
        $("#disconnectButton").on("click", app.disconnect);
        $("#course").on("input", app.updateCourse);
    },

    onDeviceReady: function()
    {
        app.refreshDeviceList();
    },

    refreshDeviceList: function()
    {
        $("#refreshButton").addClass("btnclick");
        $("#deviceList").html(""); // empties the list
        ble.scan([exofinger.serviceUUID], 5, app.onDiscoverDevice, app.onError);
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
            //ble.startNotification(deviceId, exofinger.serviceUUID, exofinger.buttonCharacteristic, app.onData, app.onError);

            $("#disconnectButton").data("deviceId", deviceId);
            target.removeClass("connection");
            target.addClass("connected");
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
            if(element.characteristic.toLowerCase() === exofinger.buttonCharacteristic)
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
            alert("Failed writing data to ExoFinger " + JSON.stringify(reason));
        };

        if(deviceId)
        {
            if(app.writeWithoutResponse)
            {
                ble.writeWithoutResponse(
                    deviceId,
                    exofinger.serviceUUID,
                    exofinger.buttonCharacteristic,
                    stringToBytes(data), success, failure
                );
            }
            else
            {
                ble.write(
                    deviceId,
                    exofinger.serviceUUID,
                    exofinger.buttonCharacteristic,
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

    opendebug: function(event)
    {
        $("#openButtondebug").addClass("btnclick");
        app.sendData(0);
    },

    closedebug: function(event)
    {
        $("#closeButtondebug").addClass("btnclick");
        app.sendData("1");
    },

    cleardebug: function(event)
    {
        $("#clearButtondebug").addClass("btnclick");
        app.sendData("R");
    },

    setCourse: function(event)
    {
        app.sendData("L" + parseFloat($("#courseDisplay").html()))
        $("#courseButton").addClass("btnhighlight");
        var value =  $("#courseDisplay").data("courseDisplay");
        if (value == null)
        {value=4;}
        $("#consignedebug").html( "± " + value + "  cm");
    },

    disconnect: function(event)
    {
        $("#disconnectButton").addClass("btnclick");
        $("#battery").removeClass("FadeIn");
        $("#batterylevel").removeClass("blink");

        $("#courseButtondebug").removeClass("btnhighlight");
        document.getElementById("course").value= "4";
        $("#courseDisplay").html(4+ " cm")
        $("#position").html(" ");
        $("#consignedebug").html(" ");
        $("#positiondebug").html(" ");

        var deviceId = $("#disconnectButton").data("deviceId");

        if(deviceId)
        {
            ble.disconnect(deviceId, app.disconnected, app.onError);
        }
    },

    disconnected: function()
    {
        $("#deviceList > li").removeClass("connected");
        $("#disconnectButton").data("deviceId", null);
    },

    onError: function(reason)
    {
        alert("ERROR: " + JSON.stringify(reason)); // real apps should use notification.alert
    },

    updateCourse: function()
    {   $("#courseButton").removeClass("btnhighlight");
        $("#courseDisplay").html($(this).val() + " cm");
        $("#courseDisplay").data("courseDisplay", $(this).val());
    }

};

app.initialize();
