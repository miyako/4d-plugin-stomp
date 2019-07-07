//%attributes = {}
$stomp:=STOMP Connect ("127.0.0.1";61613)

If ($stomp>0)
	
	ARRAY TEXT:C222($headerNames;2)
	ARRAY TEXT:C222($headerValues;2)
	
	$headerNames{1}:="login"
	$headerNames{2}:="passcode"
	$headerValues{1}:="admin"
	$headerValues{2}:="admin"
	
	$err:=_o_STOMP Write ($stomp;"CONNECT";"";$headerNames;$headerValues)
	
	  //NOTE: the array pairs are unchanged for STOMP write
	If ($err=0)
		
		$err:=_o_STOMP Read ($stomp;$command;$body;$headerNames;$headerValues)
		
		If ($err=0) & ($command="CONNECTED")
			
			ARRAY TEXT:C222($headerNames;1)
			ARRAY TEXT:C222($headerValues;1)
			
			$headerNames{1}:="destination"
			$headerValues{1}:="/queue/FOO.BAR"
			
			$err:=_o_STOMP Write ($stomp;"SUBSCRIBE";"";$headerNames;$headerValues)
			
			$body:="This is the message"
			
			$err:=_o_STOMP Write ($stomp;"SEND";$body;$headerNames;$headerValues)
			
			$err:=_o_STOMP Read ($stomp;$command;$body;$headerNames;$headerValues)
			
			$err:=_o_STOMP Write ($stomp;"DISCONNECT")
			
		End if 
		
		$err:=STOMP Disconnect ($stomp)
		
	End if 
	
End if 