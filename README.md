# ESP8226_Garage_Controller

This is an ESP8266 based controller for actuating your garage doors using your current garage
door openers and is configurable for up to 3 doors.

Integrates with any Home Automation system that supports REST API's

Samples of the inteface and Bill of Materials can be found in the "info" folder.

The controller uses the following library: https://github.com/Lymkin/myWebServerAsync

You can use a NodeMCU or my custom board that you can order from DirtyPCB's at:
https://dirtypcbs.com/store/designer/details/10941/6271/esp8266-garage-controller

A ProtoPak of 10 is $11:95 at the time I posted this.


**Home Assistant Integration Example:**

```
#####################################
# Garage Doors
#####################################
  # Door1
  - platform: command_line
    covers:
      garage_door:
        command_open: '/usr/bin/curl -X PUT -d OPEN http://172.16.1.10/api/door1/state/open'
        command_close: '/usr/bin/curl -X PUT -d CLOSED http://172.16.1.10/api/door1/state/close'
        friendly_name: "Crosstrek Garage Door"
        command_state: '/usr/bin/curl http://172.16.1.10/api/door1/state'
        value_template: >
          {% if value == 'open' %}
          100
          {% elif value == 'closed' %}
          0
          {% endif %}

  # Door2
  - platform: command_line
    covers:
      garage_door:
        command_open: '/usr/bin/curl -X PUT -d OPEN http://172.16.1.10/api/door2/state/open'
        command_close: '/usr/bin/curl -X PUT -d CLOSED http://172.16.1.10/api/door2/state/close'
        friendly_name: "Focus Garage Door"
        command_state: '/usr/bin/curl http://172.16.1.10/api/door2/state'
        value_template: >
          {% if value == 'open' %}
          100
          {% elif value == 'closed' %}
          0
          {% endif %}
  
  # Door 3
  - platform: command_line
    covers:
      garage_door:
        command_open: '/usr/bin/curl -X PUT -d OPEN http://172.16.1.10/api/door3/state/open'
        command_close: '/usr/bin/curl -X PUT -d CLOSED http://172.16.1.10/api/door3/state/close'
        friendly_name: "Truck Garage Door"
        command_state: '/usr/bin/curl http://172.16.1.10/api/door3/state'
        value_template: >
          {% if value == 'open' %}
          100
          {% elif value == 'closed' %}
          0
          {% endif %}
```