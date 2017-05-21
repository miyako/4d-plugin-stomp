# 4d-plugin-stomp
STOMP client (libstomp implementation)

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
