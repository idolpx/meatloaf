
10 rem *** open comms
11 open 15,16,15, ""
12 open 2,16,2,"https://whatsmyip.dev/api/ip"

20 rem *** receive and parse data
21 print#15, "jsonparse,2"
22 print#15, "jq,2,/addr"
23 input#2,ip$

30 rem *** close and print data
31 close 2: close 15
32 print "my ip:",ip$
