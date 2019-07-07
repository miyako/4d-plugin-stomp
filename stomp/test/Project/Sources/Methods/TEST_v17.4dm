//%attributes = {}
$stomp:=STOMP Connect ("127.0.0.1";61613)

If ($stomp>0)
	
	C_OBJECT:C1216($headers)
	
	$headers:=New object:C1471
	
	$headers.login:="passcode"
	$headers.passcode:="admin"
	
	$err:=STOMP Write ($stomp;"CONNECT";"";$headers)
	
	  //NOTE: the array pairs are unchanged for STOMP write
	If ($err=0)
		
		$headers:=New object:C1471
		
		$err:=STOMP Read ($stomp;$command;$body;$headers)
		
		If ($err=0) & ($command="CONNECTED")
			
			$headers:=New object:C1471
			
			$headers.destination:="/queue/FOO.BAR"
			
			$err:=STOMP Write ($stomp;"SUBSCRIBE";"";$headers)
			
			$body:="This is the message"
			
			$err:=STOMP Write ($stomp;"SEND";$body;$headers)
			
			$err:=STOMP Read ($stomp;$command;$body;$headers)
			
			$err:=STOMP Write ($stomp;"DISCONNECT")
			
		End if 
		
		$err:=STOMP Disconnect ($stomp)
		
	End if 
	
End if 