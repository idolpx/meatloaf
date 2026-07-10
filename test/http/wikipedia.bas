5 rem == wikipedia explorer v1 ==
6 rem == meatloaf full-mode http on device 8 ==
7 rem == uses wikimedia rest api + opensearch ==
8 ch=1
9 ub$="https://en.wikipedia.org/w/api.php?action=opensearch&search="

10 poke 53280,6:poke 53281,0:poke 646,14
15 dim tl$(9),ul$(9)
20 print chr$(147)
30 print tab(5)"*** wikipedia ***"
40 print
50 print "1. search articles"
60 print "2. random article"
70 print "3. lookup by name"
80 print "q. quit"
90 print
100 get k$:if k$="" then 100
110 if k$="q" then poke 53280,14:poke 53281,6:print chr$(147):end
120 if k$="1" then gosub 20000:goto 20
130 if k$="2" then gosub 20300:goto 20
140 if k$="3" then gosub 20400:goto 20
150 goto 100

19990 rem --- wait for any key ---
19991 get k$:if k$="" then 19991
19992 return

20000 rem --- search articles ---
20005 print chr$(147):poke 646,5
20010 print "search wikipedia":print:poke 646,14
20015 input "search term";sq$
20020 if sq$="" then return
20025 gosub 21000 : rem encode sq$ -> se$
20030 ur$=ub$+se$+"&limit=5&format=json"
20035 gosub 10000
20040 if s$<>"200" then print "http error ";s$:gosub 19990:return
20045 print chr$(147):poke 646,5:print "results:":print:poke 646,14
20050 rc=0
20055 for i=0 to 4
20060 p$="/1/"+str$(i):gosub 10100
20065 if v$="" or v$="(n/a)" then 20085
20070 tl$(rc)=v$
20075 p$="/3/"+str$(i):gosub 10100:ul$(rc)=v$
20080 rc=rc+1
20085 next i
20090 if rc=0 then print "no results":gosub 19990:close ch:return
20095 for i=0 to rc-1
20100 print i+1;".";left$(tl$(i),36)
20105 next i
20110 print:print "pick (1-";rc;") or 0=menu"
20115 get k$:if k$="" then 20115
20120 if k$="0" then close ch:return
20125 if k$<"1" or val(k$)>rc then 20115
20130 at$=tl$(val(k$)-1)
20135 close ch:gosub 20150
20140 return

21000 rem --- url encode sq$ -> se$ ---
21005 se$=""
21010 for i=1 to len(sq$)
21015 if mid$(sq$,i,1)=" " then se$=se$+"%20":goto 21025
21020 se$=se$+mid$(sq$,i,1)
21025 next i
21030 return

20150 rem --- show article summary ---
20160 tu$=""
20165 for i=1 to len(at$)
20170 if mid$(at$,i,1)=" " then tu$=tu$+"_":goto 20180
20175 tu$=tu$+mid$(at$,i,1)
20180 next i
20185 ur$="https://en.wikipedia.org/api/rest_v1/page/summary/"+tu$
20190 gosub 10000
20195 if s$<>"200" then print "error ";s$:gosub 19990:return
20200 print chr$(147):poke 646,5
20205 p$="/title":gosub 10100:print "title: ";v$
20210 print:poke 646,14
20215 p$="/description":gosub 10100:print v$
20220 print
20225 p$="/extract":gosub 10100
20230 if len(v$)>230 then v$=left$(v$,230)
20235 print v$
20240 print:print "any key=menu"
20245 gosub 19990
20250 close ch
20255 return

20300 rem --- random article ---
20305 ur$="https://en.wikipedia.org/api/rest_v1/page/random/summary"
20310 gosub 10000
20315 if s$<>"200" then print "error ";s$:gosub 19990:return
20320 print chr$(147):poke 646,5
20325 p$="/title":gosub 10100:print "title: ";v$
20330 at$=v$
20335 print:poke 646,14
20340 p$="/description":gosub 10100:print v$
20345 print
20350 p$="/extract":gosub 10100
20355 if len(v$)>230 then v$=left$(v$,230)
20360 print v$
20365 print:print "any key=menu"
20370 gosub 19990
20375 close ch
20380 return

20400 rem --- lookup by name ---
20405 print chr$(147):poke 646,5
20410 print "article lookup":print:poke 646,14
20415 input "article name";at$
20420 if at$="" then return
20425 gosub 20150
20430 return

10000 rem --- http get: ur$ -> s$=status ---
10005 close ch
10010 open ch,8,2,ur$
10015 print#ch,"m get"
10017 print#ch,"h user-agent: Meatloaf/1.0 (Commodore 64; meatloaf-firmware)"
10020 print#ch,"s"
10025 print#ch,"status"
10030 s$=""
10035 get#ch,a$
10040 if st and 64 then 10060
10045 if st and 128 then s$="-1":goto 10060
10050 if a$=chr$(13) then 10060
10055 s$=s$+a$:goto 10035
10060 return

10100 rem --- json pointer: p$ -> v$ ---
10105 print#ch,"j "+p$
10110 v$=""
10115 get#ch,a$
10120 v$=v$+a$
10122 if st and 64 then 10140
10125 if st and 128 then v$="(n/a)":goto 10140
10130 if len(v$)<250 then 10115
10140 return
