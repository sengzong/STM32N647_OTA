#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ota_gui.py - STM32N647 OTA 小软件(带描述, 多槽位) —— 无依赖(纯标准库 + 浏览器)
=============================================================================
运行:  python ota_gui.py
然后浏览器打开: http://127.0.0.1:17800
在页面选择 .ota 升级包或应用 .bin/.hex，填描述后点发送。
后端监听 8080，等待板子连接。SDK .ota 包保留应用、模型和启动配置。
"""
import base64
import json
import struct
import threading
import socket
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse
from ota_protocol import make_transfer, make_rollback, send_firmware

MAGIC = 0x3257464F   # "OFW2"
VER   = 2
BOARD_PORT = 8080
WEB_PORT   = 17800

_state = {"phase": "idle", "sent": 0, "total": 0, "file": "", "desc": "", "log": []}
_lock = threading.Lock()


def _log(msg):
    with _lock:
        _state["log"].append(msg)
        if len(_state["log"]) > 200:
            _state["log"] = _state["log"][-200:]


def _send_worker(filename, desc, payload, crc, rollback=False):
    def progress(sent, total):
        with _lock:
            _state["sent"], _state["total"] = sent, total
            if sent == total:
                _state["phase"] = "verifying"
    try:
        with _lock:
            _state.update(phase="listen", sent=0, total=len(payload), file=filename, desc=desc)
        _log(f"监听 :{BOARD_PORT}；现在复位或重新上电板子，进入独立升级窗口…")
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
            srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            srv.settimeout(180)
            srv.bind(("0.0.0.0", BOARD_PORT))
            srv.listen(1)
            conn, addr = srv.accept()
            with conn:
                _log(f"板子已连接 {addr[0]}:{addr[1]}")
                with _lock:
                    _state["phase"] = "sending"
                slot = send_firmware(conn, payload, crc, progress)
        if rollback:
            _log("板子已校验回滚备份并接受请求，即将恢复上一版固件。")
        else:
            _log(f"板子已校验并保存最新固件，CRC32={crc:08X}。")
            _log("Agent 模式将启动新固件；应用模式请在屏幕确认切换。")
        with _lock:
            _state["phase"] = "done"
    except (OSError, ValueError) as exc:
        _log(f"升级失败：{exc}")
        with _lock:
            _state["phase"] = "error"


class Handler(BaseHTTPRequestHandler):
    def _send_json(self, obj):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if urlparse(self.path).path == "/api/state":
            with _lock:
                s = dict(_state)
                s["log"] = list(_state["log"])
            self._send_json(s)
        else:
            body = HTML.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    def do_POST(self):
        ln = int(self.headers.get("Content-Length", 0))
        j = json.loads(self.rfile.read(ln))
        try:
            data = base64.b64decode(j.get("data", ""))
        except Exception:
            self._send_json({"ok": False, "err": "base64"})
            return
        fn = j.get("file", "")
        desc = j.get("desc", "").strip()
        rollback = j.get("action") == "rollback"
        try:
            if not rollback:
                payload, crc = make_transfer(fn, data, desc, j.get("version", "").strip())
            else:
                fn, desc = "回滚上一版", "回滚上一版"
                payload, crc = make_rollback()
        except (ValueError, UnicodeError) as exc:
            self._send_json({"ok": False, "err": str(exc)})
            return
        with _lock:
            busy = _state["phase"] in ("listen", "sending", "verifying")
            if not busy:
                _state["phase"] = "listen"
        if busy:
            self._send_json({"ok": False, "err": "已有升级任务，请等待完成或超时"})
            return
        t = threading.Thread(target=_send_worker, args=(fn, desc, payload, crc, rollback), daemon=True)
        t.start()
        self._send_json({"ok": True})

    def log_message(self, *a):
        pass


HTML = r"""<!DOCTYPE html><html lang="zh"><head><meta charset="utf-8">
<title>STM32N647 OTA</title>
<style>
 :root{--fg:#111;--bg:#f4f6f8;--ac:#1a7f37;--bd:#cfd6dd}
 *{box-sizing:border-box;font-family:system-ui,Segoe UI,Microsoft YaHei,sans-serif}
 body{background:var(--bg);color:var(--fg);margin:0;padding:24px;display:flex;justify-content:center}
 .card{width:520px;background:#fff;border:1px solid var(--bd);border-radius:12px;padding:22px}
 h1{font-size:20px;margin:0 0 14px}
 .drop{border:2px dashed #9aa6b0;border-radius:10px;padding:26px;text-align:center;color:#556;cursor:pointer;transition:.15s}
 .drop.over{border-color:var(--ac);background:#eaf7ee;color:var(--ac)}
 .drop b{display:block;font-size:15px}
 .fname{margin-top:10px;font-size:13px;color:#345}
 label{display:block;margin:16px 0 6px;font-size:13px;color:#556}
 input[type=text]{width:100%;padding:9px 10px;border:1px solid var(--bd);border-radius:8px;font-size:14px}
 .row{margin-top:16px;display:flex;align-items:center;gap:12px}
 button{padding:10px 22px;border:0;border-radius:8px;background:var(--ac);color:#fff;font-size:15px;cursor:pointer}
 button:disabled{background:#aab6bf;cursor:not-allowed}
 .bar{height:12px;background:#e3e8ed;border-radius:6px;overflow:hidden;margin-top:16px}
 .bar i{display:block;height:100%;width:0;background:var(--ac);transition:width .25s}
 .pct{font-size:12px;color:#556;margin-top:5px}
 .log{margin-top:14px;background:#11151a;color:#9fe8b0;border-radius:8px;padding:10px;font:12px/1.5 Consolas,monospace;height:150px;overflow:auto;white-space:pre-wrap}
 .hint{font-size:12px;color:#889;margin-top:14px}
</style></head><body><div class="card">
<h1>STM32N647 OTA · 固件升级与回滚</h1>
<div class="drop" id="drop"><b>选择 SDK 升级包 (.ota) 或应用 (.bin / .hex)</b><span>.ota 可直接发送，无需转换或改后缀</span></div>
<input type="file" id="file" accept=".bin,.hex,.ota,application/octet-stream" hidden>
<div class="fname" id="fname"></div>
<label>描述（切换界面会显示）</label>
<input type="text" id="desc" placeholder="例如：Camera demo v2" maxlength="200">
<label>固件版本号（显示在开发板，选填）</label>
<input type="text" id="version" placeholder="例如：1.2.0；留空显示“未提供”" maxlength="63">
<div class="row"><button id="send" disabled>发送新固件</button><button id="rollback">回滚上一版</button><span class="pct" id="pct"></span></div>
<div class="bar"><i id="bar"></i></div>
<div class="log" id="log"></div>
<div class="hint">电脑网卡设为 192.168.2.10。先点发送，再复位板子。SDK 的 .ota 包包含应用、对应模型和启动配置，需先安装配套的兼容版 FSBL 与 Agent。进度表示电脑发送量，写入成功以“板子已确认”为准。</div>
</div>
<script>
let fileBuf=null, fileName="";
const $=id=>document.getElementById(id);
const drop=$('drop');
['dragenter','dragover'].forEach(ev=>drop.addEventListener(ev,e=>{e.preventDefault();drop.classList.add('over');}));
['dragleave','drop'].forEach(ev=>drop.addEventListener(ev,e=>{e.preventDefault();drop.classList.remove('over');}));
drop.addEventListener('drop',e=>{const f=e.dataTransfer.files[0];if(f)load(f);});
drop.addEventListener('click',()=>$('file').click());
$('file').addEventListener('change',e=>{if(e.target.files[0])load(e.target.files[0]);});
function load(f){
  fileName=f.name;
  const r=new FileReader();
  r.onload=()=>{fileBuf=r.result;$('fname').textContent=`${fileName}  (${fileBuf.byteLength.toLocaleString()} B)`;$('send').disabled=false;};
  r.readAsArrayBuffer(f);
}
function toB64(blob){return new Promise((res,rej)=>{const r=new FileReader();r.onload=()=>res(r.result.split(',')[1]);r.onerror=rej;r.readAsDataURL(blob);});}
$('send').onclick=async()=>{
  if(!fileBuf)return;
  const desc=$('desc').value.trim();
  if(!desc){alert('请填写描述');return;}
  $('send').disabled=true;
  const b64=await toB64(new Blob([fileBuf]));
  try {
    const result=await (await fetch('/api/send',{method:'POST',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({file:fileName,desc:desc,version:$('version').value.trim(),data:b64})})).json();
    if(!result.ok){alert(result.err);$('send').disabled=false;}
  } catch(e) { alert('发送请求失败：'+e.message);$('send').disabled=false; }
};
$('rollback').onclick=async()=>{
  $('rollback').disabled=true;
  try {
    const result=await (await fetch('/api/send',{method:'POST',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({action:'rollback'})})).json();
    if(!result.ok){alert(result.err);$('rollback').disabled=false;}
  } catch(e) { alert('回滚请求失败：'+e.message);$('rollback').disabled=false; }
};
let seen=0;
setInterval(async()=>{
  let s;
  try{s=await (await fetch('/api/state')).json();}catch(e){return;}
  const p=s.total?s.sent/s.total:0;
  $('bar').style.width=(p*100).toFixed(1)+'%';
  const ph={idle:'就绪',listen:'等待板子连接…',sending:'发送中',verifying:'等待板子写入确认…',done:'板子已确认',error:'升级失败'}[(s.phase)]||s.phase;
  $('pct').textContent=`${ph}  ${s.sent? (s.sent/1024).toFixed(0)+'/':''}${s.total? (s.total/1024).toFixed(0)+' KB':''}`;
  if(['idle','done','error'].includes(s.phase)&&fileBuf)$('send').disabled=false;
  $('rollback').disabled=['listen','sending','verifying'].includes(s.phase);
  if(s.log.length>seen){$('log').textContent=s.log.join('\n');$('log').scrollTop=$('log').scrollHeight;seen=s.log.length;}
},300);
</script></body></html>"""


def main():
    srv = ThreadingHTTPServer(("0.0.0.0", WEB_PORT), Handler)
    print(f"OTA GUI 运行中 -> 浏览器打开 http://127.0.0.1:{WEB_PORT}/")
    srv.serve_forever()


if __name__ == "__main__":
    main()
