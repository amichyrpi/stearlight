"""Exercise live-audio backlog recovery and shutdown without Android hardware."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class AudioQueueTest(unittest.TestCase):
    def test_backlog_alignment_order_and_shutdown(self):
        java_home = os.environ.get('JAVA_HOME')
        javac = str(Path(java_home) / 'bin/javac') if java_home else shutil.which('javac')
        java = str(Path(java_home) / 'bin/java') if java_home else shutil.which('java')
        self.assertTrue(javac and java, 'JDK 17 is required')
        source = Path(__file__).resolve().parents[1] / 'app/src/main/java/net/stearlight/quest/PcmQueue.java'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / 'net/stearlight/quest'
            package.mkdir(parents=True)
            shutil.copyfile(source, package / source.name)
            (package / 'AudioQueueTest.java').write_text('''package net.stearlight.quest;
import java.io.IOException;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
public class AudioQueueTest {
 static byte[] frames(int start,int count){
  byte[] result=new byte[count*4];
  for(int i=0;i<count;i++)Arrays.fill(result,i*4,(i+1)*4,(byte)(start+i));
  return result;
 }
 static void expect(PcmQueue q,int start,int count)throws Exception {
  byte[] result=new byte[count*4];q.take(result);
  if(!Arrays.equals(result,frames(start,count)))throw new AssertionError("stale or misaligned PCM");
 }
 public static void main(String[] args)throws Exception {
  final PcmQueue q=new PcmQueue(24,4);
  q.put(frames(1,4),16);expect(q,1,3); // Force ring wrap.
  q.put(frames(5,4),16);expect(q,4,5);
  q.put(frames(9,6),24);q.put(frames(15,4),16);
  expect(q,13,6); // Resume at recent frames after the consumer stalls.
  if(q.droppedFrames()!=4)throw new AssertionError("backlog not accounted");
  q.put(frames(19,10),40);expect(q,23,6); // Oversized producer burst.
  if(q.droppedFrames()!=8)throw new AssertionError("oversized burst not bounded");
  for(int i=0;i<10000;i++)q.put(frames(i,1),4);
  if(q.queuedFrames()!=6)throw new AssertionError("queue grew over time");
  expect(q,9994,6);
  try{q.put(new byte[3],3);throw new AssertionError("partial frame accepted");}
  catch(IllegalArgumentException expected){}
  AtomicBoolean interrupted=new AtomicBoolean();CountDownLatch waiting=new CountDownLatch(1);
  Thread consumer=new Thread(()->{
   waiting.countDown();
   try{q.take(new byte[4]);}catch(IOException expected){interrupted.set(true);}
   catch(InterruptedException error){throw new AssertionError(error);}
  });
  consumer.start();
  if(!waiting.await(1,TimeUnit.SECONDS))throw new AssertionError("consumer did not start");
  q.close(new IOException("focus lost"));consumer.join(1000);
  if(consumer.isAlive()||!interrupted.get())throw new AssertionError("shutdown did not wake consumer");
  try{q.put(frames(1,1),4);throw new AssertionError("closed queue accepted data");}
  catch(IOException expected){}
  PcmQueue closed=new PcmQueue(16,4);closed.put(frames(1,4),16);closed.close(new IOException("disconnected"));
  try{expect(closed,1,4);throw new AssertionError("played stale frames after disconnect");}
  catch(IOException expected){}
 }
}''')
            subprocess.run([javac, '-d', str(root), *map(str, package.glob('*.java'))], check=True)
            subprocess.run([java, '-cp', str(root), 'net.stearlight.quest.AudioQueueTest'], check=True, timeout=15)


if __name__ == '__main__':
    unittest.main()
