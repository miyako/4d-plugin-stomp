# 4d-plugin-stomp
STOMP client ([libstomp](https://github.com/a3linux/libstomp) implementation)

### Platform

| carbon | cocoa | win32 | win64 |
|:------:|:-----:|:---------:|:---------:|
||<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" /> |<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" /> |<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" /> 

### Version

<img width="32" height="32" src="https://user-images.githubusercontent.com/1725068/73986501-15964580-4981-11ea-9ac1-73c5cee50aae.png"> <img src="https://user-images.githubusercontent.com/1725068/73987971-db2ea780-4984-11ea-8ada-e25fb9c3cf4e.png" width="32" height="32" />

### Syntax

[miyako.github.io](https://miyako.github.io/2019/07/07/4d-plugin-stomp.html)

### Discussion

It seems the combination of ``OB GET PROPERTY NAMES`` and ``PA_ExecuteCommandByID`` is toxic. To iterate over properties of an object property, the object is internally stringified and parsed using a 3rd party library.

to compile with Windows Visual Studio

* It is no longer necessary (in fact it will result in an error) to load ``<winsock2.h>`` before ``<windows.h>``

* Link ``Rpcrt4.lib`` for ``UuidCreate``

* Link ``Mswsock.lib`` for ``TransmitFile``
