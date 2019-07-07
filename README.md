# 4d-plugin-stomp
STOMP client ([libstomp](https://github.com/a3linux/libstomp) implementation)

### Platform

| carbon | cocoa | win32 | win64 |
|:------:|:-----:|:---------:|:---------:|
||<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" /> |<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" /> |<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" /> 

### Version

<img src="https://user-images.githubusercontent.com/1725068/41266195-ddf767b2-6e30-11e8-9d6b-2adf6a9f57a5.png" width="32" height="32" />

### Syntax

[miyako.github.io](https://miyako.github.io/2019/07/07/4d-plugin-stomp.html)

### Discussion

It seems the combination of ``OB GET PROPERTY NAMES`` and ``PA_ExecuteCommandByID`` is toxic. To iterate over properties of an object property, the object is internally stringified and parsed using a 3rd party library.

to compile with Windows Visual Studio

* It is no longer necessary (in fact it will result in an error) to load ``<winsock2.h>`` before ``<windows.h>``

* Link ``Rpcrt4.lib`` for ``UuidCreate``

* Link ``Mswsock.lib`` for ``TransmitFile``
