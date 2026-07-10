0 rem ################################################################
1 rem # openai api chat client for commodore 64
2 rem # uses meatloaf full-mode http client WITH JSON QUERY
3 rem #
4 rem # edit line 50 with your openai api key before running
5 rem #
6 rem # benefits over the original openai_chat_client.bas:
7 rem #   - no 13-byte state machine to find "content":" in raw json
8 rem #   - j /choices/0/message/content extracts the value directly
9 rem #   - response streams byte by byte, no 255-char buffer limit
10 rem #   - simpler, shorter, easier to modify
11 rem ################################################################
12 rem
13 rem ===== configuration (!! edit line 50 !!) =====
15 mo$="qwen2.5:0.5b" : rem model name
20 mx=100 : rem max tokens in response
25 ap$="http://192.168.1.131:11434/v1/chat/completions"
30 ch=1 : rem logical file number
35 er=0 : rem error flag
40 po$="" : rem user prompt / question
45 b$="" : rem response body buffer
50 ak$="" : rem api key (leave empty for ollama; set for openai)
55 rem
99 rem ===== main =====
100 print chr$(147) : rem clear screen
105 print"openai chat on commodore 64"
110 print" (with json query)"
115 print"==========================="
120 print"model: ";mo$
125 print"key: ";left$(ak$,8);"..."
130 print"max tokens: ";mx
135 print
140 print"press any key to start";
145 get k$:if k$="" goto 145
149 rem
150 rem ===== question loop =====
151 print chr$(147)
155 print"your question (empty=quit):"
160 input po$
165 if po$="" then print"ok bye!":end
170 print
175 print"sending...";
180 gosub 500 : rem build + send http request
185 if er<>0 then print:print"request failed":goto 225
190 print"ok"
192 print
195 print"response:"
200 print"---"
205 gosub 800 : rem stream extracted json value to screen
210 print"---"
220 close ch
225 print
230 print"again? (y/n)";
240 get k$:if k$="" goto 240
245 if asc(k$)=121 or asc(k$)=89 then goto 150
250 print:print"bye!":end
255 rem
499 rem ##############################################
500 rem === build json body and send http request ===
501 rem ##############################################
505 er=0
510 open ch,8,2,ap$
515 if (st and 128)<>0 then er=1:return
520 print#ch,"m post"
525 if ak$<>"" then print#ch,"h authorization: bearer ";ak$
530 print#ch,"h content-type: application/json"
535 rem
540 rem --- build json body in bq$ ---
545 bq$=chr$(123) : rem {
550 bq$=bq$+chr$(34)+"model"+chr$(34)+chr$(58)+chr$(34)+mo$+chr$(34)+chr$(44)
555 bq$=bq$+chr$(34)+"messages"+chr$(34)+chr$(58)+chr$(91) : rem "messages":[
560 bq$=bq$+chr$(123) : rem {
565 bq$=bq$+chr$(34)+"role"+chr$(34)+chr$(58)+chr$(34)+"user"+chr$(34)+chr$(44)
570 bq$=bq$+chr$(34)+"content"+chr$(34)+chr$(58)+chr$(34)+po$+chr$(34)
575 bq$=bq$+chr$(125)+chr$(93)+chr$(44) : rem }],
580 bq$=bq$+chr$(34)+"max_tokens"+chr$(34)+chr$(58)+str$(mx)
585 bq$=bq$+chr$(125) : rem }
590 rem
595 print#ch,"b ";bq$ : rem set request body
600 print#ch,"s" : rem send http request
605 gosub 650 : rem read status into st$
610 print"status: ";st$
615 if st$<>"200" then er=1:close ch:print"api error":return
618 rem
619 rem --- set json pointer query (result streamed in main) ---
620 print#ch,"j /choices/0/message/content"
625 return
628 rem
649 rem ##############################################
650 rem === read http status into st$ ===
651 rem ##############################################
655 print#ch,"status"
660 st$=""
665 get#ch,a$
670 if (st and 64)<>0 then return : rem eoi
675 if (st and 128)<>0 then return : rem error
680 if a$=chr$(13) then return : rem cr = end of status
685 st$=st$+a$
690 goto 665
695 rem
799 rem ##############################################
800 rem === stream json query result to screen ===
801 rem ##############################################
805 rem reads bytes via get# and prints them
806 rem until eoi (st bit 64) or error (st bit 128)
810 rem
815 get#ch,a$
820 if (st and 64)<>0 then print:return : rem eoi
825 if (st and 128)<>0 then print:print"err":return : rem error
830 print chr$(asc(a$)); : rem print character
835 goto 815
840 rem
