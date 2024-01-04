10 rem -------------------------------
20 rem     meatloaf chatgpt client
30 rem -------------------------------

50 ke$="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"

100 print"i'm commodore 64, ask me anything!"
110 input qu$
115 if qu$ = "" then end
120 gosub 500
130 qu$ = "":goto 100

500 rem *** prepare json ***
510 rq$ = chr$(123)+chr$(34)+"model"+chr$(34)+":"+chr$(34)+"gpt-3.5-turbo"
511 rq$=rq$+chr$(34)+","+chr$(34)+"messages"+chr$(34)
512 rq$=rq$+":["+chr$(123)+chr$(34)+"role"+chr$(34)+":"+chr$(34)
513 rq$=rq$+"user"+chr$(34)+","
514 rq$=rq$+chr$(34)+"content"+chr$(34)+":"+chr$(34)+qu$
515 rq$=rq$+chr$(34)+chr$(125)+"],"
516 rq$=rq$+chr$(34)+"temperature"+chr$(34)+":0.7"+chr$(125)
520 printchr$(158)+"debug:": print rq$ :printchr$(154)

600 rem *** open comms
610 open 15,16,15, ""
611 open 1,16,1,"https://api.openai.com/v1/chat/completions"

630 rem *** add request headers
631 print#15, "m,1,13,3": rem set headers mode
632 print#1, "Content-Type: application/json"
633 print#1, "Authorization: Bearer "+ke$

700 rem *** set json request data
710 print#15, "m,1,13,4": rem set post data mode
711 print#1, rq$

800 rem *** parse response
810 print#15, "jsonparse,1"
811 print#15, "jq,1,/choices[0]/message/content"

900 rem *** read response
910 input#1,rp$
911 printchr$(5): print rp$ :printchr$(154)

1000 rem *** close comms ***
1010 close 15
1011 close 1
1012 return
