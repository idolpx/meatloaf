10 rem *** open comms
11 open 15,16,15, ""
12 rem open 2,16,2,"https://zenquotes.io/api/today"
13 open 2,16,2,"https://zenquotes.io/api/random"

20 rem *** receive and parse data
21 print#15, "jsonparse,2"
22 print#15, "jq,2,//q"
23 input#2,q$
24 print#15, "jq,2,//a"
25 input#2,a$

30 rem *** close and print data
31 close 2: close 15
32 print chr$(14)+q$
33 print "-"+a$
