#! /bin/sh
echo "testing conversion of pressure to height"
TEST_SRCDIR=$(dirname $0)
OUT_NC="out$$.nc"
./fimex.sh \
    --input.file=${TEST_SRCDIR}/verticalPressure.nc \
    --verticalInterpolate.type=height \
    --verticalInterpolate.method=linear \
    --verticalInterpolate.level1=0,50,100,250,500,750,1000,5500,10000,20000 \
    --output.file="$OUT_NC"
if [ $? != 0 ]; then
  echo "failed converting pressure to height"
  rm -f "$OUT_NC"
  exit 1
fi
EXP_NC="${TEST_SRCDIR}/verticalPressureHeight.nc"
if "${TEST_SRCDIR}/nccmp.sh" "$EXP_NC" "$OUT_NC" ; then
  echo "success"
  E=0
else
  echo "failed diff pressure to height"
  E=1
fi
rm -f "$OUT_NC"
exit $E
