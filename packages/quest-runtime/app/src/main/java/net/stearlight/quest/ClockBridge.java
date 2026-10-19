package net.stearlight.quest;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.Arrays;

/** Linux shares Android's clock; supply its missing timezone rules. */
final class ClockBridge {
 private final File root;
 private final File[] databases;
 private String applied;
 ClockBridge(File root){this(root,new File[]{
  new File("/apex/com.android.tzdata/etc/tz/tzdata"),
  new File("/data/misc/zoneinfo/current/tzdata"),
  new File("/system/usr/share/zoneinfo/tzdata")});}
 ClockBridge(File root,File[] databases){this.root=root;this.databases=databases;}
 synchronized void sync(String zone)throws IOException {
  if(!new File(root,".stearlight-ready").isFile())return;
  File local=new File(root,"etc/localtime");
  if(!zone.matches("[A-Za-z0-9_+-]+(?:/[A-Za-z0-9_+-]+)*"))throw new IOException("Invalid timezone ID");
  File target=new File(root,"usr/share/zoneinfo/"+zone);
  Path link=Paths.get("../usr/share/zoneinfo/"+zone);
  if(zone.equals(applied)&&target.isFile()&&Files.isSymbolicLink(local.toPath())&&Files.readSymbolicLink(local.toPath()).equals(link))return;
  byte[] rules=null;
  for(File database:databases){
   if(!database.isFile()||!database.canRead())continue;
   try{rules=readZone(database,zone);break;}catch(IOException ignored){}
  }
  if(rules==null)throw new IOException("Quest timezone rules unavailable: "+zone);
  atomic(target,rules);
  atomic(new File(root,"etc/timezone"),(zone+"\n").getBytes(StandardCharsets.US_ASCII));
  Path temporary=new File(root,"etc/localtime.quest-next").toPath();
  Files.deleteIfExists(temporary);
  Files.createSymbolicLink(temporary,link);
  Files.move(temporary,local.toPath(),StandardCopyOption.REPLACE_EXISTING,StandardCopyOption.ATOMIC_MOVE);
  applied=zone;
 }
 private static void atomic(File target,byte[] data)throws IOException {
  Files.createDirectories(target.getParentFile().toPath());
  Path temporary=Paths.get(target+".quest-next");
  Files.write(temporary,data);
  Files.move(temporary,target.toPath(),StandardCopyOption.REPLACE_EXISTING,StandardCopyOption.ATOMIC_MOVE);
 }
 static byte[] readZone(File database,String zone)throws IOException {
  // Android tzdata: 24-byte header, then 52-byte index entries containing
  // a 40-byte ID, data-relative offset, length and unused raw UTC offset.
  try(RandomAccessFile file=new RandomAccessFile(database,"r")){
   byte[] header=new byte[12];file.readFully(header);
   if(!Arrays.equals(Arrays.copyOf(header,6),"tzdata".getBytes(StandardCharsets.US_ASCII))||header[11]!=0)
    throw new IOException("Invalid Android timezone database");
   int index=file.readInt(),data=file.readInt(),end=file.readInt();
   if(index<24||data<index||(data-index)%52!=0||end<data||end>file.length())throw new IOException("Invalid timezone index");
   for(long entry=index;entry<data;entry+=52){
    file.seek(entry);byte[] id=new byte[40];file.readFully(id);
    int size=0;while(size<id.length&&id[size]!=0)size++;
    int offset=file.readInt(),length=file.readInt();
    if(!zone.equals(new String(id,0,size,StandardCharsets.US_ASCII)))continue;
    if(offset<0||length<44||(long)data+offset+length>end)throw new IOException("Invalid timezone record");
    byte[] result=new byte[length];file.seek((long)data+offset);file.readFully(result);
    if(result[0]!='T'||result[1]!='Z'||result[2]!='i'||result[3]!='f')throw new IOException("Invalid TZif record");
    return result;
   }
   throw new IOException("Timezone is not in the Android database: "+zone);
  }
 }
}
