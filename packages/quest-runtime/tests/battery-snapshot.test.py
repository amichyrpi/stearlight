"""Check real battery values across charging and unavailable Android readings."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class BatteryTest(unittest.TestCase):
    def test_charging_invalid_samples_and_recovery(self):
        source = Path(__file__).resolve().parents[1] / 'app/src/main/java/net/stearlight/quest/BatterySnapshot.java'
        with tempfile.TemporaryDirectory() as folder:
            test = Path(folder) / 'BatteryTest.java'
            test.write_text('''package net.stearlight.quest;
public class BatteryTest {
 public static void main(String[] args){
  BatterySnapshot b=new BatterySnapshot();assert b.percent==-1;
  b=b.update(47,100,2,2,4000,350,1000);assert b.percent==47&&b.charging()&&b.plugged==2;
  b=b.update(-1,0,3,0,3990,349,1100);assert b.percent==47&&!b.charging()&&b.plugged==0;
  b=b.capacity(Integer.MIN_VALUE,1200);assert b.percent==47;
  b=b.update(101,100,2,2,4000,350,1300);assert b.percent==47;
  b=b.capacity(-1,32000);assert b.percent==-1;
  b=b.update(23,50,3,0,3900,340,33000);assert b.percent==46;
  b=b.capacity(45,34000);assert b.percent==45;
  b=b.update(100,100,5,1,4200,340,35000);assert b.percent==100&&b.charging();
  b=b.update(99,100,4,1,4200,340,36000);assert b.percent==99&&!b.charging()&&b.plugged==1;
  b=b.capacity(0,37000);assert b.percent==0;
 }
}''')
            subprocess.run(['javac', '-d', folder, str(source), str(test)], check=True)
            subprocess.run(['java', '-ea', '-cp', folder, 'net.stearlight.quest.BatteryTest'], check=True)

if __name__ == '__main__':
    unittest.main()
