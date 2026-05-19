0 rem http post test
1 rem test 1: read static content (GET)
2 rem test 2: write via print# (uses HTTP POST)
3 rem test 3: verify POST was called by reading stored data
10 print"{clear}=== HTTP POST TEST ==="
20 s$="192.168.1.131"
30 p$="8080"
40 print"server: http://"+s$+":"+p$
50 print""
60 print"this tests if print# on c64 calls HTTP POST"
70 print""
80 print"step 1: read /static/test (should get static content)"
90 print"step 2: print# to /posttest (uses HTTP POST)"
100 print"step 3: get /posttest to verify POST stored data"
110 print""
120 print"press key";
130 get k$:if k$="" goto 130
140 print"{clear}";
200 rem === TEST 1: Read static content ===
210 print"=== TEST 1: GET STATIC CONTENT ==="
220 open 2,8,0,"http://"+s$+":"+p$+"/static/test"
230 print"reading from /static/test..."
240 for i=1 to 20
250   get#2,a$
260   if st and 64 goto 300
270   if a$<>"" print a$
280 next i
300 close 2
310 print"--- end of static content ---"
320 print""
400 rem === TEST 2: Write via PRINT# (uses POST) ===
410 print"=== TEST 2: WRITE VIA PRINT# (HTTP POST) ==="
420 open 1,8,1,"http://"+s$+":"+p$+"/posttest"
430 print"open channel for write..."
440 print#1,"data written from c64"
450 print#1,"second line here"
460 close 1
470 print"wrote data via print#1"
480 print""
500 rem === TEST 3: Read stored data ===
510 print"=== TEST 3: VERIFY POST STORED DATA ==="
520 open 3,8,0,"http://"+s$+":"+p$+"/posttest"
530 print"reading /posttest to verify POST worked..."
540 for i=1 to 20
550   get#3,a$
560   if st and 64 goto 600
570   if a$<>"" print i;") ";a$
580 next i
600 close 3
610 print"--- end of stored data ---"
620 print""
700 rem === RESULTS ===
710 print"=== RESULTS ==="
720 print"test 1: should show 'HELLO FROM SERVER!'"
730 print"test 2: uses HTTP POST (not PUT)"
740 print"test 3: should show 'data written from c64'"
750 print"         and 'second line here'"
760 print""
770 print"if test 1 works = GET is functional"
780 print"if test 3 shows written data = POST works"
790 print""
800 print"press key to exit";
810 get k$:if k$="" goto 810
820 end