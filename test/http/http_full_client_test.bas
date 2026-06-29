0 rem ################################################################
1 rem # meatloaf full http client mode test suite                   #
2 rem #                                                              #
3 rem # tests all full-mode commands: m, h, h+, b, b+, s,           #
4 rem # r-h, r-b, c, status. echo endpoint on server                 #
5 rem # returns all request info so we can verify each test.         #
6 rem #                                                              #
7 rem # run this on a c64 with meatloaf connected.                   #
8 rem # first start the test server:                                 #
9 rem #   python3 test_server.py                                     #
10 rem # then update s$ below to your server's ip address.           #
11 rem ################################################################

12 rem === configuration ===
13 rem !!! update this to your test server's ip !!!
14 s$="192.168.1.100"
15 p$="8080"
16 b$=""                             : rem response buffer, shared

17 rem === main menu ===
18 print chr$(147)                   : rem clear screen
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
32 print"0 - run all tests"
33 print
34 print"select test";
35 get k$:if k$="" goto 35
36 t=val(k$)
37 if t=0 goto 2000
38 on t gosub 100,200,300,400,500,600,700,800,900
39 print:print"done - press key for menu";
40 get k$:if k$="" goto 40
41 goto 18

42 rem ################################################################
43 rem # helper: read a single line from channel ch into r$           #
44 rem # stops at cr ($0d) or eoi (st and 64)                        #
45 rem ################################################################
46 rem call: ch=channel, reads into r$
47 rem uses: a$, ch
48 readline:
49 r$=""
50 get#ch,a$
51 if (st and 64)<>0 then return    : rem eoi -> done
52 if (st and 128)<>0 then return   : rem error -> done
53 if a$=chr$(13) then return       : rem cr -> done
54 r$=r$+a$
55 goto 50

56 rem ################################################################
57 rem # helper: read all remaining chars from ch into b$              #
58 rem ################################################################
59 readall:
60 b$=""
61 get#ch,a$
62 if (st and 64)<>0 then return    : rem eoi -> done
63 if (st and 128)<>0 then return   : rem error -> done
64 b$=b$+a$
65 goto 61

66 rem ################################################################
67 rem # helper: wait for keypress, show prompt                       #
68 rem ################################################################
69 waitkey:
70 print"press key...";
71 get k$:if k$="" goto 71
72 return

73 rem ################################################################
74 rem # helper: read status from channel ch (via status command)     #
75 rem # returns status string in st$                                 #
76 rem ################################################################
77 getstatus:
78 print#ch,"status"
79 gosub readline
80 st$=r$
81 return

82 rem ################################################################
83 rem # helper: read all response headers from ch, store in h$       #
84 rem ################################################################
85 readheaders:
86 h$=""
87 print#ch,"r-h"
88 gosub readline                     : rem read first header
89 if r$="" goto 92                   : rem empty means done
90 h$=h$+r$+chr$(13)
91 goto 88
92 return

93 rem ################################################################
94 rem # helper: read all response body from ch into b$                #
95 rem ################################################################
96 readbody:
97 b$=""
98 print#ch,"r-b"
99 gosub readall
100 return

101 rem ################################################################
102 rem # helper: print a test verdict (pass or fail)                  #
103 rem ################################################################
104 verdict:
105 if pass then print"pass" : return
106 print"fail"
107 return

108 rem ================================================================
109 rem test 1: basic get (echo)
110 rem open url, override method to get, send, read response
111 rem ================================================================
200 print chr$(147)
201 print"=== test 1: basic get (echo) ==="
202 print
203 pass=1
204 open 1,8,2,"http://"+s$+":"+p$+"/echo"
205 if (st and 128)<>0 then print"open failed" : pass=0 : goto 299
206 print"open ok, sending commands..."
207 print#1,"m get"
208 print#1,"s"
209 print
210 print"--- status ---"
211 gosub 77                          : rem getstatus
212 print"status: ";st$
213 if st$<>"200" then print"unexpected status" : pass=0 : goto 299
214 print
215 print"--- reading response ---"
216 gosub 85                          : rem readheaders -> h$
217 print"headers:"
218 print h$
219 gosub 96                          : rem readbody -> b$
220 print"body:"
221 print left$(b$,200)
222 if b$="" then print"empty body!" : pass=0
223 print
224 gosub 69                          : rem waitkey
299 close 1
300 print:print"test 1: ";
301 gosub 104
302 return

303 rem ================================================================
304 rem test 2: post with body
305 rem ================================================================
400 print chr$(147)
401 print"=== test 2: post with body ==="
402 print
403 pass=1
404 open 1,8,2,"http://"+s$+":"+p$+"/echo"
405 if (st and 128)<>0 then print"open failed" : pass=0 : goto 499
406 print"open ok"
407 print#1,"m post"
408 print#1,"h content-type: application/json"
409 print#1,"b {""key"":""value"",""number"":42}"
410 print#1,"s"
411 print
412 print"--- status ---"
413 gosub 77
414 print"status: ";st$
415 if st$<>"200" then print"unexpected status (";st$;")" : pass=0 : goto 499
416 print
417 print"--- response ---"
418 gosub 85
419 print"headers (preview):"
420 print left$(h$,100)
421 gosub 96
422 print"body (preview):"
423 print left$(b$,200)
424 if b$="" then print"empty body!" : pass=0
425 if instr(b$,"post")=0 then print"missing post reference!" : pass=0
426 print
427 gosub 69
499 close 1
500 print:print"test 2: ";
501 gosub 104
502 return

503 rem ================================================================
504 rem test 3: headers (set + append)
505 rem ================================================================
600 print chr$(147)
601 print"=== test 3: headers (set + append) ==="
602 print
603 pass=1
604 open 1,8,2,"http://"+s$+":"+p$+"/echo"
605 if (st and 128)<>0 then print"open failed" : pass=0 : goto 699
606 print"open ok"
607 print#1,"m get"
608 rem set single header
609 print#1,"h authorization: bearer test123"
610 rem append multi-value header
611 print#1,"h+ x-custom: first"
612 print#1,"h+ x-custom: second"
613 print#1,"s"
614 gosub 77
615 print"status: ";st$
616 if st$<>"200" then print"unexpected status" : pass=0 : goto 699
617 gosub 96
618 print"body (preview):"
619 print left$(b$,250)
620 rem check header was echoed back
621 if instr(b$,"bearer test123")=0 then print"missing auth header!" : pass=0
622 if instr(b$,"x-custom")=0 then print"missing custom header!" : pass=0
623 if instr(b$,"first")=0 or instr(b$,"second")=0 then print"missing multi-value!" : pass=0
624 print
625 gosub 69
699 close 1
700 print:print"test 3: ";
701 gosub 104
702 return

703 rem ================================================================
704 rem test 4: status code test
705 rem ================================================================
800 print chr$(147)
801 print"=== test 4: status code test ==="
802 print
803 pass=1
804 open 1,8,2,"http://"+s$+":"+p$+"/echo"
805 if (st and 128)<>0 then print"open failed" : pass=0 : goto 899
806 print"open ok"
807 print"testing status command..."
808 rem do a get, check status
809 print#1,"m get"
810 print#1,"s"
811 gosub 77
812 print"1) after send, status=";st$
813 if st$<>"200" then print"expected 200, got ";st$ : pass=0
814 print
815 rem now read the body
816 gosub 96
817 print"body len: ";len(b$);" bytes"
818 print
819 gosub 69
899 close 1
900 print:print"test 4: ";
901 gosub 104
902 return

903 rem ================================================================
904 rem test 5: response header mode
905 rem ================================================================
1000 print chr$(147)
1001 print"=== test 5: response header mode ==="
1002 print
1003 pass=1
1004 open 1,8,2,"http://"+s$+":"+p$+"/echo"
1005 if (st and 128)<>0 then print"open failed" : pass=0 : goto 1099
1006 print"open ok"
1007 print#1,"m get"
1008 print#1,"s"
1009 gosub 77
1010 print"status: ";st$
1011 if st$<>"200" then print"unexpected status" : pass=0 : goto 1099
1012 print
1013 print"--- reading headers (one per line) ---"
1014 gosub 85
1015 print"(headers read into h$)"
1016 print"headers:"
1017 print left$(h$,200)
1018 if h$="" then print"no headers!" : pass=0
1019 if instr(h$,"x-request-id")=0 then print"missing x-request-id!" : pass=0
1020 if instr(h$,"x-echo-method")=0 then print"missing x-echo-method!" : pass=0
1021 print
1022 print"--- reading body ---"
1023 gosub 96
1024 print"body len: ";len(b$);" bytes"
1025 if b$="" then print"empty body!" : pass=0
1026 print
1027 gosub 69
1099 close 1
1100 print:print"test 5: ";
1101 gosub 104
1102 return

1103 rem ================================================================
1104 rem test 6: multi-request cycle (send, clear, send again)
1105 rem ================================================================
1200 print chr$(147)
1201 print"=== test 6: multi-request cycle ==="
1202 print
1203 pass=1
1204 open 2,8,2,"http://"+s$+":"+p$+"/echo"
1205 if (st and 128)<>0 then print"open failed" : pass=0 : goto 1299
1206 print"open ok"
1207 print
1208 print"--- request 1: post ---"
1209 print#2,"m post"
1210 print#2,"b first request"
1211 print#2,"s"
1212 gosub 77
1213 print"status 1: ";st$
1214 if st$<>"200" then print"unexpected status" : pass=0
1215 gosub 96
1216 print"body 1: ";left$(b$,80)
1217 print
1218 rem clear and send a different request
1219 print"--- request 2: put (after clear) ---"
1220 print#2,"c"
1221 print#2,"m put"
1222 print#2,"b second request"
1223 print#2,"s"
1224 gosub 77
1225 print"status 2: ";st$
1226 if st$<>"200" then print"unexpected status" : pass=0
1227 gosub 96
1228 print"body 2: ";left$(b$,80)
1229 if instr(b$,"put")=0 then print"missing put reference!" : pass=0
1230 if instr(b$,"second request")=0 then print"missing body text!" : pass=0
1231 print
1232 gosub 69
1299 close 2
1300 print:print"test 6: ";
1301 gosub 104
1302 return

1303 rem ================================================================
1304 rem test 7: error handling (404)
1305 rem ================================================================
1400 print chr$(147)
1401 print"=== test 7: error handling (404) ==="
1402 print
1403 pass=1
1404 open 1,8,2,"http://"+s$+":"+p$+"/echo/status/404"
1405 if (st and 128)<>0 then print"open failed" : pass=0 : goto 1499
1406 print"open ok"
1407 print#1,"m get"
1408 print#1,"s"
1409 print
1410 print"--- status (expected: 404) ---"
1411 gosub 77
1412 print"status: ";st$
1413 rem 404 is a valid http response - status may be 0 or 404 depending on impl
1414 print"(note: 404 produces a valid response, status may vary)"
1415 print
1416 print"--- reading body ---"
1417 gosub 96
1418 print"body (preview):"
1419 print left$(b$,200)
1420 if instr(b$,"404")=0 and instr(b$,"status")=0 then print"missing 404 reference!" : pass=0
1421 print
1422 gosub 69
1499 close 1
1500 print:print"test 7: ";
1501 gosub 104
1502 return

1503 rem ================================================================
1504 rem test 8: custom method (put)
1505 rem ================================================================
1600 print chr$(147)
1601 print"=== test 8: custom method (put) ==="
1602 print
1603 pass=1
1604 open 1,8,2,"http://"+s$+":"+p$+"/echo"
1605 if (st and 128)<>0 then print"open failed" : pass=0 : goto 1699
1606 print"open ok"
1607 print#1,"m put"
1608 print#1,"h content-type: text/plain"
1609 print#1,"b this is a put request"
1610 print#1,"s"
1611 gosub 77
1612 print"status: ";st$
1613 if st$<>"200" then print"unexpected status" : pass=0 : goto 1699
1614 gosub 96
1615 print"body:"
1616 print left$(b$,200)
1617 if instr(b$,"put")=0 then print"missing put reference!" : pass=0
1618 print
1619 gosub 69
1699 close 1
1700 print:print"test 8: ";
1701 gosub 104
1702 return

1703 rem ================================================================
1704 rem test 9: b+ append body test
1705 rem ================================================================
1800 print chr$(147)
1801 print"=== test 9: b+ append body ==="
1802 print
1803 pass=1
1804 open 1,8,2,"http://"+s$+":"+p$+"/echo"
1805 if (st and 128)<>0 then print"open failed" : pass=0 : goto 1899
1806 print"open ok"
1807 print#1,"m post"
1808 print#1,"h content-type: text/plain"
1809 print#1,"b hello"
1810 print#1,"b+ world"
1811 print#1,"b+ !!!"
1812 print#1,"s"
1813 gosub 77
1814 print"status: ";st$
1815 if st$<>"200" then print"unexpected status" : pass=0 : goto 1899
1816 gosub 96
1817 print"body:"
1818 print left$(b$,200)
1819 if instr(b$,"helloworld")=0 and instr(b$,"hello world")=0 then print"missing appended body!" : pass=0
1820 print
1821 gosub 69
1899 close 1
1900 print:print"test 9: ";
1901 gosub 104
1902 return

1903 rem ################################################################
1904 rem # test suite: run all tests                                    #
1905 rem ################################################################
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
2014 t$=ti$
2015 print
2016 print"all tests complete"
2017 print"press key for menu";
2018 get k$:if k$="" goto 2018
2019 goto 18
