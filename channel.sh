# 21-25, 27-29
# 21 Front West
# 23 Drip line
# 24 Front South

CHANNEL=$1
DURATION=$2
if (($CHANNEL < 21 || $CHANNEL > 24)); then
  echo "Channel $CHANNEL out of range"
  exit 1
fi

if (($DURATION < 1 || $DURATION > 15)); then
  echo "Duration $DURATION out of range"
  exit 1
fi

# disable 24VAC
gpio write 29 1
sleep 1

# disable all
gpio write 21 1
gpio write 22 1
gpio write 23 1
gpio write 24 1
gpio write 25 1
gpio write 27 1
gpio write 28 1
sleep 1

# enable 24VAC
gpio write 29 0
sleep 1

echo "Running $CHANNEL for $DURATION min ($(($DURATION * 60))s)"
gpio write $CHANNEL 0
sleep $(($DURATION * 60))
gpio write $CHANNEL 1
echo "Finished $CHANNEL"

sleep 1
# disable 24VAC
gpio write 29 1
