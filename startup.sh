# disable 24VAC first
gpio mode 29 OUT && gpio write 29 1
sleep 1

# 21-25, 27-29
# 21 Front West
gpio mode 21 OUT && gpio write 21 1
sleep 1
gpio mode 22 OUT && gpio write 22 1
sleep 1
# 23 Drip line
gpio mode 23 OUT && gpio write 23 1
sleep 1
# 24 Front South
gpio mode 24 OUT && gpio write 24 1
sleep 1
gpio mode 25 OUT && gpio write 25 1
sleep 1
gpio mode 27 OUT && gpio write 27 1
sleep 1
gpio mode 28 OUT && gpio write 28 1
