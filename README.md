# 4d-plugin-stomp
STOMP client ([libstomp](https://github.com/a3linux/libstomp) implementation)

### Platform

| carbon | cocoa | win32 | win64 |
|:------:|:-----:|:---------:|:---------:|
|<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|||

### Version

<img src="https://cloud.githubusercontent.com/assets/1725068/18940649/21945000-8645-11e6-86ed-4a0f800e5a73.png" width="32" height="32" /> <img src="https://cloud.githubusercontent.com/assets/1725068/18940648/2192ddba-8645-11e6-864d-6d5692d55717.png" width="32" height="32" />

## Syntax

```
stomp:=STOMP Connect (host;port)
```

Parameter|Type|Description
------------|------------|----
host|TEXT|
port|LONGINT|
stomp|LONGINT|

```
error:=STOMP Write (stomp;command;body;headerNames;headerValues)
error:=STOMP Read (stomp;command;body;headerNames;headerValues)
```

Parameter|Type|Description
------------|------------|----
stomp|LONGINT|
command|TEXT|
body|TEXT|
headerNames|ARRAY TEXT|
headerValues|ARRAY TEXT|
error|LONGINT|

```
error:=STOMP Disconnect (stomp)
```

Parameter|Type|Description
------------|------------|----
stomp|LONGINT|
error|LONGINT|

## Example

```
$stomp:=STOMP Connect ("127.0.0.1";61613)

If ($stomp>0)

  ARRAY TEXT($headerNames;2)
  ARRAY TEXT($headerValues;2)

  $headerNames{1}:="login"
  $headerNames{2}:="passcode"
  $headerValues{1}:="admin"
  $headerValues{2}:="admin"

  $err:=STOMP Write ($stomp;"CONNECT";"";$headerNames;$headerValues)
    //NOTE: the array pairs are unchanged for STOMP write
  If ($err=0)

    $err:=STOMP Read ($stomp;$command;$body;$headerNames;$headerValues)

    If ($err=0) & ($command="CONNECTED")

      ARRAY TEXT($headerNames;1)
      ARRAY TEXT($headerValues;1)

      $headerNames{1}:="destination"
      $headerValues{1}:="/queue/FOO.BAR"

      $err:=STOMP Write ($stomp;"SUB";"";$headerNames;$headerValues)

      $body:="This is the message"

      $err:=STOMP Write ($stomp;"SEND";$body;$headerNames;$headerValues)

      $err:=STOMP Read ($stomp;$command;$body;$headerNames;$headerValues)

      $err:=STOMP Write ($stomp;"DISCONNECT")

    End if 

    $err:=STOMP Disconnect ($stomp)

  End if 

End if 
```

**Note**: Error codes are negative translations of ``APR ERROR VALUES``. ``-1`` means the context ID was invalid.
