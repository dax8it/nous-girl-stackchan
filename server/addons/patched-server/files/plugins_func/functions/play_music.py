import os
import re
import time
import random
import difflib
import traceback
from pathlib import Path
from core.providers.tts.dto.dto import TTSMessageDTO, SentenceType, ContentType
from plugins_func.register import register_function, ToolType, ActionResponse, Action
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from core.connection import ConnectionHandler

TAG = __name__

MUSIC_CACHE = {}

play_music_function_desc = {
    "type": "function",
    "function": {
        "name": "play_music",
        "description": "当用户要求播放音乐、歌曲时调用。",
        "parameters": {
            "type": "object",
            "properties": {
                "song_name": {
                    "type": "string",
                    "description": "歌曲名称，如果用户没有指定具体歌名则为'random', 明确指定的时返回音乐的名字 示例: ```用户:播放两只老虎\n参数：两只老虎``` ```用户:播放音乐 \n参数：random ```",
                }
            },
            "required": ["song_name"],
        },
    },
}


@register_function("play_music", play_music_function_desc, ToolType.SYSTEM_CTL)
async def play_music(conn: "ConnectionHandler", song_name: str):
    try:
        music_intent = (
            f"播放音乐 {song_name}" if song_name != "random" else "随机播放音乐"
        )
        await handle_music_command(conn, music_intent)
        return ActionResponse(
            action=Action.RECORD, result="指令已接收", response="正在为您播放音乐"
        )
    except Exception as e:
        conn.logger.bind(tag=TAG).error(f"处理音乐意图错误: {e}")
        return ActionResponse(
            action=Action.RESPONSE, result=str(e), response="播放音乐时出错了"
        )


def _extract_song_name(text):
    """从用户输入中提取歌名"""
    for keyword in ["播放音乐"]:
        if keyword in text:
            parts = text.split(keyword)
            if len(parts) > 1:
                return parts[1].strip()
    return None


def _find_best_match(potential_song, music_files):
    """查找最匹配的歌曲"""
    best_match = None
    highest_ratio = 0

    for music_file in music_files:
        song_name = os.path.splitext(music_file)[0]
        ratio = difflib.SequenceMatcher(None, potential_song, song_name).ratio()
        if ratio > highest_ratio and ratio > 0.4:
            highest_ratio = ratio
            best_match = music_file
    return best_match


def get_music_files(music_dir, music_ext):
    music_dir = Path(music_dir)
    music_files = []
    music_file_names = []
    for file in music_dir.rglob("*"):
        # 判断是否是文件
        if file.is_file():
            # 获取文件扩展名
            ext = file.suffix.lower()
            # 判断扩展名是否在列表中
            if ext in music_ext:
                # 添加相对路径
                music_files.append(str(file.relative_to(music_dir)))
                music_file_names.append(
                    os.path.splitext(str(file.relative_to(music_dir)))[0]
                )
    return music_files, music_file_names


def initialize_music_handler(conn: "ConnectionHandler"):
    global MUSIC_CACHE
    if MUSIC_CACHE == {}:
        plugins_config = conn.config.get("plugins", {})
        if "play_music" in plugins_config:
            MUSIC_CACHE["music_config"] = plugins_config["play_music"]
            MUSIC_CACHE["music_dir"] = os.path.abspath(
                MUSIC_CACHE["music_config"].get("music_dir", "./music")  # 默认路径修改
            )
            MUSIC_CACHE["music_ext"] = MUSIC_CACHE["music_config"].get(
                "music_ext", (".mp3", ".wav", ".p3")
            )
            MUSIC_CACHE["refresh_time"] = MUSIC_CACHE["music_config"].get(
                "refresh_time", 60
            )
        else:
            MUSIC_CACHE["music_dir"] = os.path.abspath("./music")
            MUSIC_CACHE["music_ext"] = (".mp3", ".wav", ".p3")
            MUSIC_CACHE["refresh_time"] = 60
        # 获取音乐文件列表
        MUSIC_CACHE["music_files"], MUSIC_CACHE["music_file_names"] = get_music_files(
            MUSIC_CACHE["music_dir"], MUSIC_CACHE["music_ext"]
        )
        MUSIC_CACHE["scan_time"] = time.time()
    return MUSIC_CACHE


async def handle_music_command(conn: "ConnectionHandler", text):
    initialize_music_handler(conn)
    global MUSIC_CACHE

    """处理音乐播放指令"""
    clean_text = re.sub(r"[^\w\s]", "", text).strip()
    conn.logger.bind(tag=TAG).debug(f"检查是否是音乐命令: {clean_text}")

    # 尝试匹配具体歌名
    if os.path.exists(MUSIC_CACHE["music_dir"]):
        if time.time() - MUSIC_CACHE["scan_time"] > MUSIC_CACHE["refresh_time"]:
            # 刷新音乐文件列表
            MUSIC_CACHE["music_files"], MUSIC_CACHE["music_file_names"] = (
                get_music_files(MUSIC_CACHE["music_dir"], MUSIC_CACHE["music_ext"])
            )
            MUSIC_CACHE["scan_time"] = time.time()

        potential_song = _extract_song_name(clean_text)
        if potential_song:
            best_match = _find_best_match(potential_song, MUSIC_CACHE["music_files"])
            if best_match:
                conn.logger.bind(tag=TAG).info(f"找到最匹配的歌曲: {best_match}")
                await play_local_music(conn, specific_file=best_match)
                return True
    # 检查是否是通用播放音乐命令
    await play_local_music(conn)
    return True


def _get_random_play_prompt(song_name):
    """生成随机播放引导语"""
    # 移除文件扩展名
    clean_name = os.path.splitext(song_name)[0]
    # headphone-girl patch: English intros for the English Kokoro voice
    prompts = [
        f"Now playing {clean_name}!",
        f"Here comes {clean_name}!",
        f"Let's listen to {clean_name}!",
        f"Dance time! This is {clean_name}.",
    ]
    # 直接使用random.choice，不设置seed
    return random.choice(prompts)


async def play_local_music(conn: "ConnectionHandler", specific_file=None, record=True):
    global MUSIC_CACHE
    """播放本地音乐文件
    v3.6.7-dj: record=False when hg_skip already moved the play history."""
    try:
        if not os.path.exists(MUSIC_CACHE["music_dir"]):
            conn.logger.bind(tag=TAG).error(
                f"音乐目录不存在: " + MUSIC_CACHE["music_dir"]
            )
            return

        # 确保路径正确性
        if specific_file:
            selected_music = specific_file
            music_path = os.path.join(MUSIC_CACHE["music_dir"], specific_file)
        else:
            if not MUSIC_CACHE["music_files"]:
                conn.logger.bind(tag=TAG).error("未找到MP3音乐文件")
                return
            selected_music = random.choice(MUSIC_CACHE["music_files"])
            music_path = os.path.join(MUSIC_CACHE["music_dir"], selected_music)

        if not os.path.exists(music_path):
            conn.logger.bind(tag=TAG).error(f"选定的音乐文件不存在: {music_path}")
            return
        try:   # v3.6.7-radio: a server song ends the radio quietly
            from core.handle.radioHandler import stop_for_other_media
            stop_for_other_media(conn)
        except Exception:
            pass
        if record:
            hg_history(conn).played(selected_music)   # v3.6.7-dj: real prev / next
        text = (_hg_dj_prompt(selected_music) if getattr(conn, "hg_dj", False)
                else _get_random_play_prompt(selected_music))
        conn.tts.store_tts_text(conn.sentence_id, text)
        # conn.dialogue.put(Message(role="assistant", content=text))

        if conn.intent_type == "intent_llm":
            conn.tts.tts_text_queue.put(
                TTSMessageDTO(
                    sentence_id=conn.sentence_id,
                    sentence_type=SentenceType.FIRST,
                    content_type=ContentType.ACTION,
                )
            )
        conn.tts.tts_text_queue.put(
            TTSMessageDTO(
                sentence_id=conn.sentence_id,
                sentence_type=SentenceType.MIDDLE,
                content_type=ContentType.TEXT,
                content_detail=text,
            )
        )
        # headphone-girl v3: ask the robot to dance while the song plays
        _hg_start_groove(conn)
        # v3.6.7-dj: title for the DJ screen (no-op on older firmware)
        _hg_device_tool(conn, "self_dj_mode",
                        {"action": "track", "track": os.path.splitext(os.path.basename(selected_music))[0]})
        conn.tts.tts_text_queue.put(
            TTSMessageDTO(
                sentence_id=conn.sentence_id,
                sentence_type=SentenceType.MIDDLE,
                content_type=ContentType.FILE,
                content_file=music_path,
            )
        )
        if conn.intent_type == "intent_llm":
            conn.tts.tts_text_queue.put(
                TTSMessageDTO(
                    sentence_id=conn.sentence_id,
                    sentence_type=SentenceType.LAST,
                    content_type=ContentType.ACTION,
                )
            )

    except Exception as e:
        conn.logger.bind(tag=TAG).error(f"播放音乐失败: {str(e)}")
        conn.logger.bind(tag=TAG).error(f"详细错误: {traceback.format_exc()}")


# ── headphone-girl v3 ────────────────────────────────────────────────────────
def _hg_start_groove(conn):
    """Fire-and-forget: device tool self.dance.groove (dance to the beat).
    Does nothing if the robot doesn't have the tool or it is turned off."""
    try:
        import asyncio
        from core.providers.tools.device_mcp.mcp_handler import call_mcp_tool
        client = getattr(conn, "mcp_client", None)
        if client is None or not client.has_tool("self_dance_groove"):
            return
        asyncio.get_event_loop().create_task(
            call_mcp_tool(conn, client, "self_dance_groove", '{"on": true}', timeout=10))
    except Exception as e:
        conn.logger.bind(tag=TAG).debug(f"groove call skipped: {e}")


# ── v3.6.7-dj: play history (real next / previous) and DJ mode ─────────────
class PlayHistory:
    """Browser-style history, same rules as the firmware's hg_order::TrackHistory
    (hg_dj_logic.h). played() records a fresh pick and drops "forward" entries.
    next() walks forward through history, then continues in sorted library order
    after the current track. prev() walks back, then continues in library order
    before the first track. Tracks no longer in the library are dropped."""

    def __init__(self, cap=50):
        self.h = []
        self.i = 0
        self.cap = max(2, int(cap))

    def current(self):
        return self.h[self.i] if self.h else ""

    def _trim_front(self):
        while len(self.h) > self.cap:
            self.h.pop(0)
            if self.i > 0:
                self.i -= 1

    def played(self, t):
        if not t:
            return
        if self.h and self.i + 1 < len(self.h):
            del self.h[self.i + 1:]
        if self.h and self.h[-1] == t:
            self.i = len(self.h) - 1
            return
        self.h.append(t)
        self._trim_front()
        self.i = len(self.h) - 1

    def next(self, lib):
        lib_set = set(lib)
        while self.h and self.i + 1 < len(self.h):
            j = self.i + 1
            if self.h[j] in lib_set:
                self.i = j
                return self.h[j]
            del self.h[j]
        order = sorted(lib_set)
        if not order:
            return ""
        cur = self.current()
        pick = order[(order.index(cur) + 1) % len(order)] if cur in lib_set else order[0]
        self.h.append(pick)
        self._trim_front()
        self.i = len(self.h) - 1
        return pick

    def prev(self, lib):
        lib_set = set(lib)
        while self.h and self.i > 0:
            j = self.i - 1
            if self.h[j] in lib_set:
                self.i = j
                return self.h[j]
            del self.h[j]
            self.i -= 1
        order = sorted(lib_set)
        if not order:
            return ""
        cur = self.current()
        pick = order[(order.index(cur) - 1) % len(order)] if cur in lib_set else order[-1]
        self.h.insert(0, pick)
        if len(self.h) > self.cap:
            self.h.pop()
        self.i = 0
        return pick


HG_HISTORY = {}   # device id -> PlayHistory (lives as long as the server process)


def hg_history(conn):
    key = getattr(conn, "device_id", None) or "default"
    h = HG_HISTORY.get(key)
    if h is None:
        h = HG_HISTORY[key] = PlayHistory()
    return h


def _hg_dj_prompt(song_name):
    clean_name = os.path.splitext(os.path.basename(song_name))[0]
    return random.choice([
        f"DJ Nous-Girl on the decks! This is {clean_name}.",
        f"Spinning {clean_name}!",
        f"Next on the turntable: {clean_name}!",
        f"Drop the needle! {clean_name}.",
    ])


def _hg_device_tool(conn, tool, args):
    """Fire-and-forget device MCP call; silently skipped if the robot lacks the tool."""
    try:
        import asyncio
        import json as _json
        from core.providers.tools.device_mcp.mcp_handler import call_mcp_tool
        client = getattr(conn, "mcp_client", None)
        if client is None or not client.has_tool(tool):
            return False
        asyncio.get_event_loop().create_task(
            call_mcp_tool(conn, client, tool, _json.dumps(args), timeout=10))
        return True
    except Exception as e:
        conn.logger.bind(tag=TAG).debug(f"{tool} call skipped: {e}")
        return False


def hg_set_dj(conn, on):
    """DJ mode on/off: remembered on the connection (intros) and saved on the robot."""
    conn.hg_dj = bool(on)
    return _hg_device_tool(conn, "self_dj_mode", {"action": "on" if on else "off"})


def _hg_library(conn):
    cache = initialize_music_handler(conn)
    if os.path.exists(cache["music_dir"]) and \
            time.time() - cache.get("scan_time", 0) > cache.get("refresh_time", 60):
        cache["music_files"], cache["music_file_names"] = get_music_files(
            cache["music_dir"], cache["music_ext"])
        cache["scan_time"] = time.time()
    return cache.get("music_files", [])


async def hg_skip(conn, direction):
    """Play the next or previous song from the play history. Caller wraps FIRST/LAST
    when needed. Returns the file played, or None if the library is empty."""
    files = _hg_library(conn)
    h = hg_history(conn)
    pick = h.prev(files) if direction == "prev" else h.next(files)
    if not pick:
        return None
    conn.logger.bind(tag=TAG).info(f"music {direction}: {pick}")
    await play_local_music(conn, specific_file=pick, record=False)
    return pick


def _hg_norm(text):
    t = re.sub(r"[^\w\s']", " ", text or "").lower()
    return re.sub(r"\s+", " ", t).strip()


_HG_LEAD = r"^(?:(?:ok|okay|hey|um|uh|so|please|nous girl|new girl|noose girl)\s+)*"
_HG_TAIL = r"(?:\s+(?:please|now|again))*$"
_HG_SONG = r"(?:song|track|tune|one)"
_HG_NEXT_RE = re.compile(r"(?:" + _HG_LEAD + r"(?:(?:play|put on|go to)\s+)?(?:the\s+)?next\s+" + _HG_SONG
                         + r"|" + _HG_LEAD + r"skip\s+(?:this\s+|the\s+)?(?:song|track|tune))" + _HG_TAIL)
_HG_PREV_RE = re.compile(r"(?:" + _HG_LEAD + r"(?:(?:play|put on|go to|go back to)\s+)?(?:the\s+)?"
                         r"(?:previous|last|prior)\s+" + _HG_SONG
                         + r"|" + _HG_LEAD + r"go back\s+(?:a|one)\s+(?:song|track)"
                         + r"|" + _HG_LEAD + r"back\s+one\s+(?:song|track))" + _HG_TAIL)
_HG_DJ = r"(?:dj|d j|deejay|dee jay|dj's)"
_HG_DJ_ON_RE = re.compile(_HG_LEAD + r"(?:(?:start|enter|turn on|switch to|go into|go to|activate|"
                          r"let's do|lets do|let's go|do|begin)\s+)?(?:the\s+)?" + _HG_DJ
                          + r"\s+(?:mode|time|set)(?:\s+on)?" + _HG_TAIL
                          + r"|" + _HG_LEAD + r"be\s+(?:my|the|our)\s+" + _HG_DJ + _HG_TAIL)
_HG_DJ_OFF_RE = re.compile(_HG_LEAD + r"(?:(?:stop|end|exit|quit|leave|turn off|cancel)\s+(?:the\s+)?"
                           + _HG_DJ + r"(?:\s+mode)?|" + _HG_DJ + r"\s+mode\s+off)" + _HG_TAIL)


def hg_music_command(text):
    """-> "next" | "prev" | "dj_on" | "dj_off" | None (pure, unit-tested)."""
    t = _hg_norm(text)
    if not t:
        return None
    if _HG_DJ_OFF_RE.match(t):
        return "dj_off"
    if _HG_DJ_ON_RE.match(t):
        return "dj_on"
    if _HG_NEXT_RE.match(t):
        return "next"
    if _HG_PREV_RE.match(t):
        return "prev"
    return None


async def _hg_say(conn, text):
    import uuid
    conn.sentence_id = str(uuid.uuid4().hex)
    conn.tts.tts_text_queue.put(TTSMessageDTO(sentence_id=conn.sentence_id,
                                              sentence_type=SentenceType.FIRST,
                                              content_type=ContentType.ACTION))
    conn.tts.tts_text_queue.put(TTSMessageDTO(sentence_id=conn.sentence_id,
                                              sentence_type=SentenceType.MIDDLE,
                                              content_type=ContentType.TEXT,
                                              content_detail=text))
    conn.tts.tts_text_queue.put(TTSMessageDTO(sentence_id=conn.sentence_id,
                                              sentence_type=SentenceType.LAST,
                                              content_type=ContentType.ACTION))


async def hg_try_dj_shortcut(conn, text, setlists=True):
    """v3.6.7-dj voice: "next song", "previous song", "DJ mode", "DJ mode off".
    v3.6.7-radio: radio phrases are tried first (see radioHandler.py).
    DJ set lists (data/setlists, core/handle/setlistHandler.py) get the first look:
    while a set plays, next / previous step through its songs; "DJ mode" offers the
    set lists when there are any. Returns True if handled.
    Off with plugins.play_music.hg_voice_shortcut: false."""
    cfg = conn.config.get("plugins", {}).get("play_music", {})
    if not cfg.get("hg_voice_shortcut", True):
        return False
    if setlists:
        # v3.6.7-radio: internet radio first (core/handle/radioHandler.py, data/radio/stations.json).
        # While the radio is on or paused, next / previous (the screen swipe) change station.
        from core.handle.radioHandler import hg_try_radio
        if await hg_try_radio(conn, text):
            return True
        from core.handle.setlistHandler import hg_try_setlist
        if await hg_try_setlist(conn, text):
            return True
    cmd = hg_music_command(text)
    if cmd is None:
        return False
    if cmd == "dj_off":
        hg_set_dj(conn, False)
        await _hg_say(conn, "DJ mode off.")
        return True
    if cmd == "dj_on":
        hg_set_dj(conn, True)
        cmd = "next"
    files = _hg_library(conn)
    if not files:
        return False          # no songs: let the LLM answer
    import uuid
    conn.sentence_id = str(uuid.uuid4().hex)
    wrap = conn.intent_type != "intent_llm"
    if wrap:
        conn.tts.tts_text_queue.put(TTSMessageDTO(sentence_id=conn.sentence_id,
                                                  sentence_type=SentenceType.FIRST,
                                                  content_type=ContentType.ACTION))
    await hg_skip(conn, cmd)
    if wrap:
        conn.tts.tts_text_queue.put(TTSMessageDTO(sentence_id=conn.sentence_id,
                                                  sentence_type=SentenceType.LAST,
                                                  content_type=ContentType.ACTION))
    return True


_HG_PLAY_RE = re.compile(
    r"^(?:please\s+)?(?:can you\s+|could you\s+)?(?:play|put on)\s+(?P<what>.+)$", re.I)
_HG_GENERIC = {"music", "some music", "a song", "something", "a tune", "songs", "my music",
               "some songs", "anything", "a random song", "random music"}


async def hg_try_music_shortcut(conn: "ConnectionHandler", text):
    """English voice shortcut that works with Intent=nointent:
    "play music", "play <song name>", "put on <song>". Returns True if handled.
    Turn off with plugins.play_music.hg_voice_shortcut: false."""
    cfg = conn.config.get("plugins", {}).get("play_music", {})
    if not cfg.get("hg_voice_shortcut", True):
        return False
    # v3.6.7-rave BEGIN: "rave mode" / "start a rave" / "rave mode off" / "stop the rave" go first
    # (core/handle/raveHandler.py). True = handled; a str = the rewritten request, e.g. "continue",
    # "next song", "stop" or "play <name>", for the radio / set list / music shortcuts below.
    try:
        from core.handle.raveHandler import hg_try_rave
        rave = await hg_try_rave(conn, text)
    except Exception as e:
        conn.logger.bind(tag=TAG).warning(f"rave shortcut skipped: {e}")
        rave = None
    if rave is True:
        return True
    if isinstance(rave, str) and rave:
        text = rave
    # v3.6.7-rave END
    # v3.6.7-dj: next / previous / DJ mode first ("play the next song" is not a title)
    if await hg_try_dj_shortcut(conn, text):
        return True
    m = _HG_PLAY_RE.match(re.sub(r"[^\w\s']", " ", text or "").strip())
    if not m:
        return False
    what = re.sub(r"\s+", " ", m.group("what")).strip().lower()
    what = re.sub(r"^(the\s+)?(song|track)\s+", "", what)
    initialize_music_handler(conn)
    files = MUSIC_CACHE.get("music_files", [])
    if not files:
        return False
    target = None
    if what not in _HG_GENERIC:
        names = {os.path.splitext(os.path.basename(f))[0].lower(): f for f in files}
        hit = difflib.get_close_matches(what, list(names), n=1, cutoff=0.55)
        if not hit:
            hit = [n for n in names if what in n][:1]
        if not hit:
            return False          # let the LLM answer ("play" might mean a game)
        target = names[hit[0]]
    import uuid
    conn.sentence_id = str(uuid.uuid4().hex)
    wrap = conn.intent_type != "intent_llm"   # play_local_music only wraps for intent_llm
    if wrap:
        conn.tts.tts_text_queue.put(TTSMessageDTO(sentence_id=conn.sentence_id,
                                                  sentence_type=SentenceType.FIRST,
                                                  content_type=ContentType.ACTION))
    await play_local_music(conn, specific_file=target)
    if wrap:
        conn.tts.tts_text_queue.put(TTSMessageDTO(sentence_id=conn.sentence_id,
                                                  sentence_type=SentenceType.LAST,
                                                  content_type=ContentType.ACTION))
    return True
