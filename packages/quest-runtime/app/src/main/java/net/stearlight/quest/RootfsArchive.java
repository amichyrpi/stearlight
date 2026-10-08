package net.stearlight.quest;

import android.system.Os;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.zip.GZIPInputStream;

/** Extracts the generated rootfs archive into the app's private Linux tree. */
final class RootfsArchive {
 private static long number(byte[] header,int offset,int length) throws IOException {
  String value=text(header,offset,length).trim();
  if(value.isEmpty()) return 0;
  try{return Long.parseLong(value,8);}catch(NumberFormatException e){throw new IOException("Invalid tar number",e);}
 }
 private static String text(byte[] header,int offset,int length){int end=offset;while(end<offset+length&&header[end]!=0)end++;return new String(header,offset,end-offset,StandardCharsets.UTF_8);}
 private static void readFully(InputStream in,byte[] bytes) throws IOException {int n=0;while(n<bytes.length){if(Thread.currentThread().isInterrupted())throw new InterruptedIOException("Rootfs installation cancelled");int got=in.read(bytes,n,bytes.length-n);if(got<0)throw new EOFException("Incomplete rootfs archive");n+=got;}}
 private static void copy(InputStream in,OutputStream out,long count) throws IOException {byte[] bytes=new byte[131072];while(count>0){if(Thread.currentThread().isInterrupted())throw new InterruptedIOException("Rootfs installation cancelled");int n=in.read(bytes,0,(int)Math.min(bytes.length,count));if(n<0)throw new EOFException("Incomplete rootfs file");if(out!=null)out.write(bytes,0,n);count-=n;}}
 private static String safeName(String name) throws IOException {
  while(name.startsWith("./"))name=name.substring(2);
  if(name.startsWith("/"))throw new IOException("Absolute archive path");
  for(String component:name.split("/"))if(component.equals(".."))throw new IOException("Archive path escapes rootfs");
  return name;
 }
 private static File destination(File root,String name) throws IOException {
  File target=new File(root,safeName(name));String base=root.getCanonicalPath(),real=target.getCanonicalPath();
  if(!real.equals(base)&&!real.startsWith(base+File.separator))throw new IOException("Archive link escapes rootfs");return target;
 }
 private static Map<String,String> pax(byte[] payload) throws IOException {
  Map<String,String> result=new HashMap<>();int offset=0;
  while(offset<payload.length){int space=offset;while(space<payload.length&&payload[space]!=' ')space++;if(space==payload.length)throw new IOException("Invalid PAX record");int length=Integer.parseInt(new String(payload,offset,space-offset,StandardCharsets.US_ASCII));if(length<=space-offset+1||offset+length>payload.length)throw new IOException("Invalid PAX length");String value=new String(payload,space+1,offset+length-space-2,StandardCharsets.UTF_8);int equals=value.indexOf('=');if(equals>0)result.put(value.substring(0,equals),value.substring(equals+1));offset+=length;}return result;
 }
 static void extract(InputStream source,File root) throws Exception {
  if(!root.mkdirs()&&!root.isDirectory())throw new IOException("Cannot create rootfs");
  Map<String,String> attributes=new HashMap<>();List<String[]> links=new ArrayList<>();
  try(InputStream in=new BufferedInputStream(new GZIPInputStream(source,131072),131072)){
   byte[] header=new byte[512];
   while(true){readFully(in,header);boolean empty=true;for(byte b:header)if(b!=0){empty=false;break;}if(empty)break;
    long checksum=number(header,148,8),sum=0;for(int i=0;i<512;i++)sum+=(i>=148&&i<156)?32:(header[i]&255);if(sum!=checksum)throw new IOException("Invalid tar header checksum");
    String prefix=text(header,345,155),name=text(header,0,100);if(!prefix.isEmpty())name=prefix+"/"+name;
    long size=number(header,124,12);int mode=(int)number(header,100,8)&0777;int type=header[156];String link=text(header,157,100);
    if(type=='x'||type=='g') {if(size>1048576)throw new IOException("PAX header too large");byte[] data=new byte[(int)size];readFully(in,data);attributes.putAll(pax(data));}
    else {
     name=attributes.getOrDefault("path",name);link=attributes.getOrDefault("linkpath",link);if(attributes.containsKey("size"))size=Long.parseLong(attributes.get("size"));attributes.clear();
     File target=destination(root,name);File parent=target.getParentFile();if(parent!=null&&!parent.mkdirs()&&!parent.isDirectory())throw new IOException("Cannot create rootfs directory");
     if(type=='5'){if(!target.mkdirs()&&!target.isDirectory())throw new IOException("Cannot create rootfs directory");Os.chmod(target.toString(),mode|0700);copy(in,null,size);}
     else if(type=='2'){if(link.startsWith("/"))throw new IOException("Rootfs symlinks must be normalized");File resolved=new File(target.getParentFile(),link);String base=root.getCanonicalPath(),real=resolved.getCanonicalPath();if(!real.equals(base)&&!real.startsWith(base+"/"))throw new IOException("Symlink escapes rootfs");if(java.nio.file.Files.exists(target.toPath(),java.nio.file.LinkOption.NOFOLLOW_LINKS))java.nio.file.Files.delete(target.toPath());Os.symlink(link,target.toString());copy(in,null,size);}
     else if(type=='1'){links.add(new String[]{name,safeName(link)});copy(in,null,size);}
     else if(type==0||type=='0'){try(OutputStream out=new BufferedOutputStream(new FileOutputStream(target),131072)){copy(in,out,size);}Os.chmod(target.toString(),mode);}
     else {copy(in,null,size);}
    }
    copy(in,null,(512-(size%512))%512);
   }
   // Read through the gzip trailer so truncation and CRC failures are caught
   // before the extracted rootfs is accepted as a complete installation.
   byte[] trailer=new byte[131072];while(in.read(trailer)!=-1){}
  }
  for(String[] link:links){File from=destination(root,link[1]),to=destination(root,link[0]);try{Os.link(from.toString(),to.toString());}catch(android.system.ErrnoException error){if(error.errno!=android.system.OsConstants.EACCES&&error.errno!=android.system.OsConstants.EPERM)throw new IOException("Cannot create rootfs hardlink "+link[0]+" from "+link[1],error);java.nio.file.Files.copy(from.toPath(),to.toPath());Os.chmod(to.toString(),Os.stat(from.toString()).st_mode&0777);}}
 }
}
