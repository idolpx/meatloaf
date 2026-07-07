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
200 gosub 800 : rem parse + print response
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
610 if st$<>"200" then er=1:close ch:return
615 print#ch,"r-b" : rem switch to body mode
620 gosub 700 : rem read body into b$
625 close ch
630 return
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
800 rem === parse "content" from json in b$ ===
801 rem === prints response to screen        ===
802 rem === searches for "content":"<text>"   ===
803 rem ##############################################
805 nd$=chr$(34)+"content"+chr$(34)+chr$(58)+chr$(34) : rem "content":"
810 hs$=b$
815 gosub 900 : rem string search for needle
820 if in=0 then print"[could not parse response]":print left$(b$,200):er=1:return
825 p=i+len(nd$) : rem position right after the opening "
830 rc$=""
835 for i=p to len(b$)
837 c$=mid$(b$,i,1)
838 if c$=chr$(92) and mid$(b$,i+1,1)=chr$(34) then rc$=rc$+chr$(34):i=i+1:goto 850 : rem handle \"
840 if c$=chr$(34) then print rc$:return : rem closing quote, done
845 rc$=rc$+c$
850 if len(rc$)>240 then print rc$:return : rem safety limit
855 next i
860 print rc$:return
865 rem
899 rem ##############################################
900 rem === string search: hs$=haystack, nd$=needle ===
901 rem === returns in=1 at pos i, or in=0 ============
902 rem ##############################################
905 if len(hs$)=0 or len(nd$)=0 or len(nd$)>len(hs$) then in=0:return
910 for i=1 to len(hs$)-len(nd$)+1
915 if mid$(hs$,i,len(nd$))=nd$ then in=1:return
920 next i
925 in=0:return
