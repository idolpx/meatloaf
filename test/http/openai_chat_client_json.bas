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
14 15 mo$="qwen2.5:0.5b" : rem model name
15 20 mx=100 : rem max tokens in response
16 25 ap$="http://192.168.50.1:11434/v1/chat/completions"
17 30 ch=1 : rem logical file number
18 35 er=0 : rem error flag
19 40 po$="" : rem user prompt / question
20 45 b$="" : rem response body buffer
21 50 ak$="" : rem api key (leave empty for ollama; set for openai)
22 55 rem
23 99 rem ===== main =====
24 100 print chr$(147) : rem clear screen
25 105 print"openai chat on commodore 64"
26 110 print" (with json query)"
27 115 print"==========================="
28 120 print"model: ";mo$
29 125 print"key: ";left$(ak$,8);"..."
30 130 print"max tokens: ";mx
31 135 print
32 140 print"press any key to start";
33 145 get k$:if k$="" goto 145
34 149 rem
35 150 rem ===== question loop =====
36 151 print chr$(147)
37 155 print"your question (empty=quit):"
38 160 input po$
39 165 if po$="" then print"ok bye!":end
40 170 print
41 175 print"sending...";
42 180 gosub 500 : rem build + send http request
43 185 if er<>0 then print:print"request failed":goto 225
44 190 print"ok"
45 192 print
46 195 print"response:"
47 200 print"---"
48 205 gosub 800 : rem stream extracted json value to screen
49 210 print"---"
50 220 close ch
51 225 print
52 230 print"again? (y/n)";
53 240 get k$:if k$="" goto 240
54 245 if asc(k$)=121 or asc(k$)=89 then goto 150
55 250 print:print"bye!":end
56 255 rem
57 499 rem ##############################################
58 500 rem === build json body and send http request ===
59 501 rem ##############################################
60 505 er=0
61 510 open ch,8,2,ap$
62 515 if (st and 128)<>0 then er=1:return
63 520 print#ch,"m post"
64 525 if ak$<>"" then print#ch,"h authorization: bearer ";ak$
65 530 print#ch,"h content-type: application/json"
66 535 rem
67 540 rem --- build json body in bq$ ---
68 545 bq$=chr$(123) : rem {
69 550 bq$=bq$+chr$(34)+"model"+chr$(34)+chr$(58)+chr$(34)+mo$+chr$(34)+chr$(44)
70 555 bq$=bq$+chr$(34)+"messages"+chr$(34)+chr$(58)+chr$(91) : rem "messages":[
71 560 bq$=bq$+chr$(123) : rem {
72 565 bq$=bq$+chr$(34)+"role"+chr$(34)+chr$(58)+chr$(34)+"user"+chr$(34)+chr$(44)
73 570 bq$=bq$+chr$(34)+"content"+chr$(34)+chr$(58)+chr$(34)+po$+chr$(34)
74 575 bq$=bq$+chr$(125)+chr$(93)+chr$(44) : rem }],
75 580 bq$=bq$+chr$(34)+"max_tokens"+chr$(34)+chr$(58)+str$(mx)
76 585 bq$=bq$+chr$(125) : rem }
77 590 rem
78 595 print#ch,"b ";bq$ : rem set request body
79 600 print#ch,"s" : rem send http request
80 605 gosub 650 : rem read status into st$
81 610 print"status: ";st$
82 615 if st$<>"200" then er=1:close ch:print"api error":return
83 618 rem
84 619 rem --- set json pointer query (result streamed in main) ---
85 620 print#ch,"j /choices/0/message/content"
86 625 return
87 628 rem
88 649 rem ##############################################
89 650 rem === read http status into st$ ===
90 651 rem ##############################################
91 655 print#ch,"status"
92 660 st$=""
93 665 get#ch,a$
94 670 if (st and 64)<>0 then return : rem eoi
95 675 if (st and 128)<>0 then return : rem error
96 680 if a$=chr$(13) then return : rem cr = end of status
97 685 st$=st$+a$
98 690 goto 665
99 695 rem
100 799 rem ##############################################
101 800 rem === stream json query result to screen ===
102 801 rem ##############################################
103 805 rem reads bytes via get# and prints them
104 806 rem until eoi (st bit 64) or error (st bit 128)
105 810 rem
106 815 get#ch,a$
107 820 if (st and 64)<>0 then print:return : rem eoi
108 825 if (st and 128)<>0 then print:print"err":return : rem error
109 830 print chr$(asc(a$)); : rem print character
110 835 goto 815
111 840 rem
