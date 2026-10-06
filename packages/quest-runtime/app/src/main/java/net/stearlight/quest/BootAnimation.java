package net.stearlight.quest;

import android.content.res.AssetManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.os.SystemClock;
import org.json.JSONObject;
import org.json.JSONArray;
import java.io.InputStream;
import java.io.IOException;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/** Original Steam Frame movie, then its loading loop, shared by both eyes. */
final class BootAnimation {
 private final AssetManager assets;
 private final Bitmap bitmap = Bitmap.createBitmap(450,450,Bitmap.Config.ARGB_8888);
 private final Canvas canvas = new Canvas(bitmap);
 private JSONObject clips;
 private long started;
 private String current;
 BootAnimation(AssetManager assets) { this.assets=assets; }
 private void initialize() throws Exception {
  if(clips!=null)return;
  try(InputStream in=assets.open("boot/clips.json")){
   ByteArrayOutputStream bytes=new ByteArrayOutputStream();
   byte[] buffer=new byte[4096];int count;while((count=in.read(buffer))!=-1)bytes.write(buffer,0,count);
   clips=new JSONObject(new String(bytes.toByteArray(),StandardCharsets.UTF_8));
  }
  started=SystemClock.elapsedRealtime();
 }
 synchronized boolean bootComplete() {
  try { initialize(); return SystemClock.elapsedRealtime()-started>=clips.getJSONObject("startup").getLong("duration_ms"); }
  catch(Exception e) { throw new IllegalStateException("Cannot load Steam Frame boot animation",e); }
 }
 synchronized Bitmap frame() {
  try {
   initialize();
   long elapsed=SystemClock.elapsedRealtime()-started;
   long bootDuration=clips.getJSONObject("startup").getLong("duration_ms");
   String name=elapsed<bootDuration?"startup":"loop";
   JSONObject clip=clips.getJSONObject(name);
   if(name.equals("loop"))elapsed=(elapsed-bootDuration)%clip.getLong("duration_ms");
   JSONArray starts=clip.getJSONArray("starts_ms");
   int index=0;while(index+1<starts.length()&&starts.getLong(index+1)<=elapsed)index++;
   String path=String.format(Locale.ROOT,"boot/%s-%04d.png",name,index+1);
   if(!path.equals(current)){
    try(InputStream in=assets.open(path)){
     Bitmap decoded=BitmapFactory.decodeStream(in);
     if(decoded==null)throw new IOException("Invalid animation frame "+path);
     canvas.drawColor(0xff000000);
     canvas.save();canvas.translate(0,450);canvas.scale(1,-1);
     canvas.drawBitmap(decoded,(450-decoded.getWidth())/2f,(450-decoded.getHeight())/2f,null);
     canvas.restore();decoded.recycle();current=path;
    }
   }
   return bitmap;
  }catch(Exception e){throw new IllegalStateException("Cannot decode Steam Frame boot animation",e);}
 }
}
