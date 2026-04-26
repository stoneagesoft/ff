variable v   0 v !
: bench  50000000 0 do  v @  1 +  v !  loop ;
bench  v @ drop  bye
