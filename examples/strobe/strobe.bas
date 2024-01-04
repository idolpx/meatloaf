10 poke 53280, 0
20 poke 53281, 1
25 gosub 60
30 poke 53280, 1
40 poke 53281, 0
45 gosub 60
50 goto 10
60 rem delay loop
62 for i=1to10:nexti
64 return