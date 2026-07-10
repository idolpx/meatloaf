0 rem ################################################################
1 rem # meatloaf full http client mode test suite
2 rem # tests: m, h, h+, b, b+, s, r-h, r-b, c, status
3 rem # run on c64 with meatloaf. first start test server:
4 rem #   python3 test/http/test_server.py
5 rem # then update s$ below to your server's ip
6 rem ################################################################
7 rem === configuration ===
8 rem !!! update this to your test server's ip !!!
9 s$="192.168.1.131"
10 p$="8080"
11 b$="" : rem response buffer
12 rem skip to menu
13 goto 17
17 rem === menu ===
18 print chr$(147)
19 print"meatloaf full http client test"
20 print"================================"
21 print"server: http://"+s$+":"+p$
22 print
23 print"1 - basic get (echo)"
24 print"2 - post with body"
25 print"3 - headers (set + append)"
26 print"4 - status code test"
27 print"5 - response header mode"
28 print"6 - multi-request cycle"
29 print"7 - error handling (404)"
30 print"8 - custom method (put)"
31 print"9 - b+ append body test"
315 print"10 - json query"
32 print"0 - run all tests"
33 print
34 print"select test";
35 get k$:if k$="" goto 35
36 t=val(k$)
37 if t=0 goto 2000
38 on t gosub 200,400,600,800,1000,1200,1400,1600,1800,30000
39 print:print"done - press key for menu";
40 get k$:if k$="" goto 40
41 goto 18
42 rem === helpers ===
43 rem all use ch=channel, set before calling
44 rem --- read one line from ch into r$ ---
45 r$=""
46 get#ch,a$
47 if (st and 64)<>0 then return : rem eoi
48 if (st and 128)<>0 then return : rem error
49 if a$=chr$(13) then return : rem cr
50 r$=r$+a$
51 goto 46
52 rem --- read all remaining from ch into b$ (max 250) ---
53 b$="":bc=0
54 get#ch,a$
55 if (st and 64)<>0 then return : rem eoi
56 if (st and 128)<>0 then return : rem error
57 if bc>249 then return : rem avoid string too long
58 bc=bc+1:b$=b$+a$
59 goto 54
60 rem --- wait for keypress ---
61 print"press key...";
62 get k$:if k$="" goto 62
63 return
64 rem --- read status from ch into st$ ---
65 print#ch,"status"
66 gosub 45
67 st$=r$
68 return
69 rem --- read response headers from ch into h$ (drains to empty line) ---
70 h$=""
71 print#ch,"r-h"
72 gosub 45
73 if r$="" then return : rem empty line = end of headers
74 if len(h$)+len(r$)+1 > 230 then goto 72 : rem overflow: skip accumulate, drain rest
75 h$=h$+r$+chr$(13)
76 goto 72
77 goto 73
78 rem --- read all response body from ch into b$ ---
79 b$=""
80 print#ch,"r-b"
81 gosub 53
82 return
83 rem --- verdict: if pass<>0 print pass else fail ---
84 if pass then print"pass" : return
85 print"fail"
86 return
86 rem ################################################################
87 rem test 1: basic get
88 rem ################################################################
200 print chr$(147)
201 print"=== test 1: basic get (echo) ==="
202 print
203 pass=1
204 ch=1
205 open 1,8,2,"http://"+s$+":"+p$+"/echo"
206 if (st and 128)<>0 then print"open failed" : pass=0 : goto 299
207 print"open ok, sending commands..."
208 print#1,"m get"
209 print#1,"s"
210 print
211 print"--- status ---"
212 gosub 64
213 print"status: ";st$
214 if st$<>"200" then print"unexpected status" : pass=0 : goto 299
215 print
216 print"--- reading response ---"
217 gosub 69
218 print"headers:"
219 print h$
220 gosub 77
221 print"body:"
222 if b$="" then print"empty body!" : pass=0
223 print left$(b$,200)
224 print
225 gosub 60
299 close 1
300 print:print"test 1: ";
301 gosub 82
302 return
303 rem ################################################################
304 rem test 2: post with body
305 rem ################################################################
400 print chr$(147)
401 print"=== test 2: post with body ==="
402 print
403 pass=1
404 ch=1
405 open 1,8,2,"http://"+s$+":"+p$+"/echo"
406 if (st and 128)<>0 then print"open failed" : pass=0 : goto 499
407 print"open ok"
408 print#1,"m post"
409 print#1,"h content-type: application/json"
410 print#1,"b {""key"":""value"",""number"":42}"
411 print#1,"s"
412 print
413 print"--- status ---"
414 gosub 64
415 print"status: ";st$
416 if st$<>"200" then print"unexpected status (";st$;")" : pass=0 : goto 499
417 print
418 print"--- response ---"
419 gosub 69
420 print"headers (preview):"
421 print left$(h$,100)
422 gosub 77
423 print"body (preview):"
424 if b$="" then print"empty body!" : pass=0
425 nd$="POST":hs$=b$:gosub 1903:if in=0 then print"missing post reference!" : pass=0
426 print left$(b$,200)
427 print
428 gosub 60
499 close 1
500 print:print"test 2: ";
501 gosub 82
502 return
503 rem ################################################################
504 rem test 3: headers (set + append)
505 rem ################################################################
600 print chr$(147)
601 print"=== test 3: headers (set + append) ==="
602 print
603 pass=1
604 ch=1
605 open 1,8,2,"http://"+s$+":"+p$+"/echo"
606 if (st and 128)<>0 then print"open failed" : pass=0 : goto 699
607 print"open ok"
608 print#1,"m get"
609 print#1,"h authorization: bearer test123"
610 print#1,"h+ x-custom: first"
611 print#1,"h+ x-custom: second"
612 print#1,"s"
613 gosub 64
614 print"status: ";st$
615 if st$<>"200" then print"unexpected status" : pass=0 : goto 699
616 gosub 77
617 print"body (preview):"
618 print left$(b$,250)
619 hs$=b$:nd$="bearer test123":gosub 1903:if in=0 then print"missing auth header!" : pass=0
620 hs$=b$:nd$="x-custom":gosub 1903:if in=0 then print"missing custom header!" : pass=0
621 hs$=b$:nd$="first":gosub 1903:if in=0 then print"missing first value!" : pass=0
622 hs$=b$:nd$="second":gosub 1903:if in=0 then print"missing second value!" : pass=0
623 print
624 gosub 60
699 close 1
700 print:print"test 3: ";
701 gosub 82
702 return
703 rem ################################################################
704 rem test 4: status code test
705 rem ################################################################
800 print chr$(147)
801 print"=== test 4: status code test ==="
802 print
803 pass=1
804 ch=1
805 open 1,8,2,"http://"+s$+":"+p$+"/echo"
806 if (st and 128)<>0 then print"open failed" : pass=0 : goto 899
807 print"open ok"
808 print#1,"m get"
809 print#1,"s"
810 gosub 64
811 print"1) after send, status=";st$
812 if st$<>"200" then print"expected 200, got ";st$ : pass=0
813 print
814 gosub 77
815 print"body len: ";len(b$);" bytes"
816 print
817 gosub 60
899 close 1
900 print:print"test 4: ";
901 gosub 82
902 return
903 rem ################################################################
904 rem test 5: response header mode
905 rem ################################################################
1000 print chr$(147)
1001 print"=== test 5: response header mode ==="
1002 print
1003 pass=1
1004 ch=1
1005 open 1,8,2,"http://"+s$+":"+p$+"/echo"
1006 if (st and 128)<>0 then print"open failed" : pass=0 : goto 1099
1007 print"open ok"
1008 print#1,"m get"
1009 print#1,"s"
1010 gosub 64
1011 print"status: ";st$
1012 if st$<>"200" then print"unexpected status" : pass=0 : goto 1099
1013 print
1014 print"--- reading headers (one per line) ---"
1015 gosub 69
1016 print"(headers read into h$)"
1017 print"headers:"
1018 print left$(h$,200)
1019 if h$="" then print"no headers!" : pass=0
1020 hs$=h$:nd$="x-request-id":gosub 1903:if in=0 then print"missing x-request-id!" : pass=0
1021 hs$=h$:nd$="x-echo-method":gosub 1903:if in=0 then print"missing x-echo-method!" : pass=0
1022 print
1023 print"--- reading body ---"
1024 gosub 77
1025 print"body len: ";len(b$);" bytes"
1026 if b$="" then print"empty body!" : pass=0
1027 print
1028 gosub 60
1099 close 1
1100 print:print"test 5: ";
1101 gosub 82
1102 return
1103 rem ################################################################
1104 rem test 6: multi-request cycle
1105 rem ################################################################
1200 print chr$(147)
1201 print"=== test 6: multi-request cycle ==="
1202 print
1203 pass=1
1204 ch=2
1205 open 2,8,2,"http://"+s$+":"+p$+"/echo"
1206 if (st and 128)<>0 then print"open failed" : pass=0 : goto 1299
1207 print"open ok"
1208 print
1209 print"--- request 1: post ---"
1210 print#2,"m post"
1211 print#2,"b first request"
1212 print#2,"s"
1213 gosub 64
1214 print"status 1: ";st$
1215 if st$<>"200" then print"unexpected status" : pass=0
1216 gosub 77
1217 print"body 1: ";left$(b$,80)
1218 print
1219 print"--- request 2: put (after clear) ---"
1220 print#2,"c"
1221 print#2,"m put"
1222 print#2,"b second request"
1223 print#2,"s"
1224 gosub 64
1225 print"status 2: ";st$
1226 if st$<>"200" then print"unexpected status" : pass=0
1227 gosub 77
1228 print"body 2: ";left$(b$,80)
1229 hs$=b$:nd$="put":gosub 1903:if in=0 then print"missing put reference!" : pass=0
1230 hs$=b$:nd$="second request":gosub 1903:if in=0 then print"missing body text!" : pass=0
1231 print
1232 gosub 60
1299 close 2
1300 print:print"test 6: ";
1301 gosub 82
1302 return
1303 rem ################################################################
1304 rem test 7: error handling (404)
1305 rem ################################################################
1400 print chr$(147)
1401 print"=== test 7: error handling (404) ==="
1402 print
1403 pass=1
1404 ch=1
1405 open 1,8,2,"http://"+s$+":"+p$+"/echo/status/404"
1406 if (st and 128)<>0 then print"open failed" : pass=0 : goto 1499
1407 print"open ok"
1408 print#1,"m get"
1409 print#1,"s"
1410 print
1411 print"--- status (expected: 404) ---"
1412 gosub 64
1413 print"status: ";st$
1414 print"(note: 404 produces a valid response, status may vary)"
1415 print
1416 print"--- reading body ---"
1417 gosub 77
1418 print"body (preview):"
1419 print left$(b$,200)
1420 hs$=b$:nd$="404":gosub 1903:if in=0 then hs$=b$:nd$="status":gosub 1903:if in=0 then print"missing 404 reference!" : pass=0
1421 print
1422 gosub 60
1499 close 1
1500 print:print"test 7: ";
1501 gosub 82
1502 return
1503 rem ################################################################
1504 rem test 8: custom method (put)
1505 rem ################################################################
1600 print chr$(147)
1601 print"=== test 8: custom method (put) ==="
1602 print
1603 pass=1
1604 ch=1
1605 open 1,8,2,"http://"+s$+":"+p$+"/echo"
1606 if (st and 128)<>0 then print"open failed" : pass=0 : goto 1699
1607 print"open ok"
1608 print#1,"m put"
1609 print#1,"h content-type: text/plain"
1610 print#1,"b this is a put request"
1611 print#1,"s"
1612 gosub 64
1613 print"status: ";st$
1614 if st$<>"200" then print"unexpected status" : pass=0 : goto 1699
1615 gosub 77
1616 print"body:"
1617 print left$(b$,200)
1618 hs$=b$:nd$="put":gosub 1903:if in=0 then print"missing put reference!" : pass=0
1619 print
1620 gosub 60
1699 close 1
1700 print:print"test 8: ";
1701 gosub 82
1702 return
1703 rem ################################################################
1704 rem test 9: b+ append body test
1705 rem ################################################################
1800 print chr$(147)
1801 print"=== test 9: b+ append body ==="
1802 print
1803 pass=1
1804 ch=1
1805 open 1,8,2,"http://"+s$+":"+p$+"/echo"
1806 if (st and 128)<>0 then print"open failed" : pass=0 : goto 1899
1807 print"open ok"
1808 print#1,"m post"
1809 print#1,"h content-type: text/plain"
1810 print#1,"b hello"
1811 print#1,"b+ world"
1812 print#1,"b+ !!!"
1813 print#1,"s"
1814 gosub 64
1815 print"status: ";st$
1816 if st$<>"200" then print"unexpected status" : pass=0 : goto 1899
1817 gosub 77
1818 print"body:"
1819 print left$(b$,200)
1820 hs$=b$:nd$="helloworld":gosub 1903:if in=0 then hs$=b$:nd$="hello world":gosub 1903:if in=0 then print"missing appended body!" : pass=0
1821 print
1822 gosub 60
1899 close 1
1900 print:print"test 9: ";
1901 gosub 82
1902 return
1903 rem --- string search helper: hs$=haystack, nd$=needle ---
1904 rem returns in=1 if found, in=0 if not (cbmbasic v2 has no instr())
1905 if len(hs$)=0 or len(nd$)=0 or len(nd$)>len(hs$) then in=0:return
1906 for i=1 to len(hs$)-len(nd$)+1
1907 if mid$(hs$,i,len(nd$))=nd$ then in=1:return
1908 next i
1909 in=0:return

30000 rem ################################################################
30001 rem test 10: json query command
30002 rem ################################################################
30003 print chr$(147):print"=== test 10: json query ===":print
30005 pass=1:ch=1
30010 open 1,8,2,"http://"+s$+":"+p$+"/echo/json"
30015 if (st and 128)<>0 then print"open failed":pass=0:goto 30099
30020 print"open ok"
30025 print#1,"m get":print#1,"s"
30030 print#1,"j /message"
30032 gosub 30190
30034 print"string: ";left$(r$,80)
30036 hs$=r$:nd$="Hello from Meatloaf":gosub 1903
30038 if in=0 then print"UNEXPECTED STRING!":pass=0:goto 30099
30040 print"string query: ok"
30042 print#1,"j /choices/0/message/content"
30044 gosub 30190
30046 print"nested: ";left$(r$,80)
30048 hs$=r$:nd$="extracted content":gosub 1903
30050 if in=0 then print"UNEXPECTED NESTED!":pass=0:goto 30099
30052 print"nested query: ok"
30054 print#1,"j /code"
30056 gosub 30190
30058 print"number: ";r$
30060 if r$<>"200" then print"expected 200!":pass=0:goto 30099
30062 print"number query: ok"
30064 print#1,"j /active"
30066 gosub 30190
30068 print"boolean: ";r$
30070 if r$<>"TRUE" then print"expected TRUE!":pass=0
30072 print"boolean query: ok"
30074 gosub 60
30099 close 1
30100 print:print"test 10: ";
30102 gosub 82
30104 return
30190 rem --- read json query value into r$ (max 249) ---
30192 r$="":bc=0
30194 get#1,a$:if (st and 64)<>0 then return
30196 if bc>248 then return
30198 bc=bc+1:r$=r$+a$:goto 30194

1910 rem ################################################################
1911 rem test suite: run all tests
1912 rem ################################################################
2000 print chr$(147)
2001 print"running all tests..."
2002 print"===================="
2003 print
2004 t$=ti$
2005 gosub 200
2006 gosub 400
2007 gosub 600
2008 gosub 800
2009 gosub 1000
2010 gosub 1200
2011 gosub 1400
2012 gosub 1600
2013 gosub 1800
20135 gosub 30000
2014 t$=ti$
2015 print
2016 print"all tests complete"
2017 print"press key for menu";
2018 get k$:if k$="" goto 2018
2019 goto 18
