0 rem ################################################################
1 rem # openai api chat client for commodore 64
2 rem # uses meatloaf full-mode http client
3 rem #
4 rem # edit line 50 with your openai api key before running
5 rem #
6 rem # limitations:
7 rem # - basic v2 strings max 255 chars; long responses are truncated
8 rem # - user input with double-quotes may break the json body
9 rem # - json parser is naive: stops at first unescaped "
10 rem ################################################################
11 rem
12 rem ===== configuration (!! edit line 50 !!) =====
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
110 print"==========================="
115 print"model: ";mo$
120 print"key: ";left$(ak$,8);"..."
125 print"max tokens: ";mx
130 print
135 print"press any key to start";
140 get k$:if k$="" goto 140
145 rem
149 rem ===== question loop =====
150 print chr$(147)
155 print"your question (empty=quit):"
160 input po$
165 if po$="" then print"ok bye!":end
170 print
175 print"sending...";
180 gosub 500 : rem build + send http request
185 if er=0 then print"ok":goto 195
190 print:print"request failed":goto 215
195 print
200 rem body already streamed by gosub 500
205 print
210 print"---"
215 print"again? (y/n)";
220 get k$:if k$="" goto 220
225 if k$="y" then goto 150
230 print:print"bye!":end
235 rem
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
612 print#ch,"r-h" : rem drain headers
613 get#ch,a$:if a$=chr$(13) then 615:goto 613
615 gosub 800 : rem stream + parse body from channel
620 close ch
625 return
630 rem
635 rem
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
699 rem ##############################################
700 rem === read response body into b$ (max 249) ===
701 rem ##############################################
705 b$="":bc=0
710 get#ch,a$
715 if (st and 64)<>0 then return : rem eoi = body done
720 if (st and 128)<>0 then return : rem error
725 if bc>248 then return : rem string overflow guard
730 bc=bc+1:b$=b$+a$
735 goto 710
740 rem
799 rem ##############################################
800 rem === stream body, find "CONTENT":" then  ===
801 rem === print chars until closing quote     ===
802 rem === avoids 255-char string limit         ===
803 rem ##############################################
805 print#ch,"r-b"
806 mc=0
807 get#ch,a$
808 if (st and 64)<>0 then print:return : rem eoi
809 if (st and 128)<>0 then print:return : rem err
810 if mc>=0 then 820 : rem searching for "content":" (needle = 13 bytes)
811 rem streaming: mc=-1 means already in content
812 if a$=chr$(34) then print:return : rem closing quote
813 print chr$(asc(a$));:goto 807
820 rem state machine: match "content":" char by char
821 if a$=chr$(34) and mc=0 then mc=1:goto 807
822 if a$=chr$(99) and mc=1 then mc=2:goto 807
823 if a$=chr$(111) and mc=2 then mc=3:goto 807
824 if a$=chr$(110) and mc=3 then mc=4:goto 807
825 if a$=chr$(116) and mc=4 then mc=5:goto 807
826 if a$=chr$(101) and mc=5 then mc=6:goto 807
827 if a$=chr$(110) and mc=6 then mc=7:goto 807
828 if a$=chr$(116) and mc=7 then mc=8:goto 807
829 if a$=chr$(34) and mc=8 then mc=9:goto 807
830 if a$=chr$(58) and mc=9 then mc=10:goto 807
831 if a$=chr$(34) and mc=10 then mc=-1:goto 807 : rem found!
832 mc=0:goto 807
899 rem ##############################################
900 rem === string search: hs$=haystack, nd$=needle ===
901 rem === returns in=1 at pos i, or in=0 ============
902 rem ##############################################
905 if len(hs$)=0 or len(nd$)=0 or len(nd$)>len(hs$) then in=0:return
910 for i=1 to len(hs$)-len(nd$)+1
915 if mid$(hs$,i,len(nd$))=nd$ then in=1:return
920 next i
925 in=0:return
