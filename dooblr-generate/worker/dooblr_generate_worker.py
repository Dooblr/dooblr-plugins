#!/usr/bin/env python3
"""
dooblr-generate worker: runs text-to-audio models in a separate process for the dooblr-generate
plug-in, so a model crash, a hang or a slow first load can never take the DAW down.

The plug-in starts one worker per job (download, check or generate) and reads one JSON object per
line from stdout:

    {"event": "progress", "stage": "loading" | "sampling" | "decoding" | "writing" | "downloading",
     "fraction": 0.0..1.0, "message": "..."}
    {"event": "done", ...result...}
    {"event": "error", "code": "...", "message": "..."}

Exit codes: 0 ok, 2 bad arguments, 3 model not installed, 4 Hugging Face access needed,
5 Python runtime missing a package, 6 generation failed, 7 download failed, 8 internal error.
The worker never calls abort(): every failure is a JSON error line and a non-zero exit.

Models (weights are never bundled with dooblr; they are downloaded or located by the user):
  stable-audio-open-small  stabilityai/stable-audio-open-small (Stability AI Community License,
                           gated: accept the terms on Hugging Face and supply a token)
  audioldm-s-full-v2       cvssp/audioldm-s-full-v2 (CC BY-NC-SA 4.0: non-commercial only)
  test-backend             NOT AI: a deterministic synthetic noise/tone burst for tests.

Only the standard library is needed for "test-backend"; the real models import torch lazily.
"""

import argparse
import json
import math
import os
import random
import struct
import sys
import threading
import time
import traceback
import wave

WORKER_VERSION = "3"

MODELS = {
    "stable-audio-open-small": {
        "repo": "stabilityai/stable-audio-open-small",
        "gated": True,
        "allow": ["model_config.json", "model.safetensors", "LICENSE", "README.md"],
        "required": ["model_config.json", "model.safetensors"],
        "max_seconds": 11.0,
        "steps": 8,
    },
    "audioldm-s-full-v2": {
        "repo": "cvssp/audioldm-s-full-v2",
        "gated": False,
        "allow": ["*.json", "*.txt", "*.safetensors"],
        "required": ["model_index.json", "unet/diffusion_pytorch_model.safetensors",
                     "vae/diffusion_pytorch_model.safetensors", "vocoder/model.safetensors",
                     "text_encoder/model.safetensors"],
        "max_seconds": 10.0,
        "steps": 25,
    },
    "test-backend": {"repo": None, "gated": False, "allow": [], "required": [], "max_seconds": 10.0, "steps": 1},
}

ACCESS_HELP = ("Stable Audio Open Small is gated on Hugging Face. Sign in at huggingface.co, open "
               "https://huggingface.co/stabilityai/stable-audio-open-small and accept the Stability AI "
               "Community License, then create a read-only access token at "
               "https://huggingface.co/settings/tokens and paste it into dooblr-generate (Token...). "
               "Or download the files yourself and choose the folder (Folder...).")


def emit(event, **fields):
    fields["event"] = event
    sys.stdout.write(json.dumps(fields) + "\n")
    sys.stdout.flush()


def progress(stage, fraction, message=""):
    emit("progress", stage=stage, fraction=round(max(0.0, min(1.0, fraction)), 4), message=message)


def fail(code, message, exit_code):
    emit("error", code=code, message=message)
    sys.exit(exit_code)


def missing_files(model_id, model_dir):
    return [f for f in MODELS[model_id]["required"] if not os.path.isfile(os.path.join(model_dir, f))]


def read_token(path):
    if not path:
        return None
    try:
        with open(path, "r", encoding="utf-8") as f:
            token = f.read().strip()
        return token or None
    except OSError:
        return None


# --------------------------------------------------------------------------------------------
# WAV output (24-bit PCM, peak-normalised to -1 dBFS), standard library only.
def write_wav(path, channels, sample_rate):
    """channels: list of lists of floats (one per channel, equal length)."""
    peak = max((abs(x) for ch in channels for x in ch), default=0.0)
    gain = (10 ** (-1.0 / 20.0)) / peak if peak > 1e-9 else 1.0
    n = len(channels[0]) if channels else 0
    frames = bytearray()
    for i in range(n):
        for ch in channels:
            v = int(round(max(-1.0, min(1.0, ch[i] * gain)) * 8388607.0))
            frames += struct.pack("<i", v)[0:3]
    tmp = path + ".part"
    with wave.open(tmp, "wb") as w:
        w.setnchannels(len(channels))
        w.setsampwidth(3)
        w.setframerate(int(sample_rate))
        w.writeframes(bytes(frames))
    os.replace(tmp, path)
    return n


def write_wav_numpy(path, audio, sample_rate):
    """audio: numpy array (channels, samples). Uses numpy for speed; same format as write_wav."""
    import numpy as np
    audio = np.asarray(audio, dtype=np.float64)
    if audio.ndim == 1:
        audio = audio[None, :]
    peak = float(np.max(np.abs(audio))) if audio.size else 0.0
    if peak > 1e-9:
        audio = audio * ((10 ** (-1.0 / 20.0)) / peak)
    ints = np.round(np.clip(audio, -1.0, 1.0) * 8388607.0).astype("<i4").T.copy()
    raw = ints.view(np.uint8).reshape(-1, 4)[:, :3].tobytes()
    tmp = path + ".part"
    with wave.open(tmp, "wb") as w:
        w.setnchannels(audio.shape[0])
        w.setsampwidth(3)
        w.setframerate(int(sample_rate))
        w.writeframes(raw)
    os.replace(tmp, path)
    return audio.shape[1]


# --------------------------------------------------------------------------------------------
def pick_device(torch):
    if os.environ.get("DOOBLR_GENERATE_DEVICE"):
        return os.environ["DOOBLR_GENERATE_DEVICE"]
    if torch.backends.mps.is_available():
        return "mps"
    return "cpu"


RUNTIME_HELP = ("The Python runtime is broken or incomplete. Reinstall it: run "
                "dooblr-generate/scripts/setup-runtime.sh (it installs the pinned, tested versions from "
                "dooblr-generate/worker/requirements-lock.txt).")


def runtime_self_check(model_id):
    """Imports the model's whole Python stack before any model work, so a broken environment
    (e.g. a compiled package built for a different NumPy ABI) is reported up front as a
    'runtime' error (exit 5) instead of a traceback half way through a generation."""
    if model_id == "test-backend":
        return {}
    import importlib
    stacks = {
        # Everything create_model_from_config() + generate_diffusion_cond() import for this model
        # (stable-audio-tools' DiT imports pytorch_lightning through its LoRA helpers).
        "stable-audio-open-small": ["numpy", "scipy", "torch", "torchaudio", "pywt", "transformers", "pytorch_lightning",
                                    "stable_audio_tools.models.factory", "stable_audio_tools.models.conditioners",
                                    "stable_audio_tools.models.pretransforms", "stable_audio_tools.models.autoencoders",
                                    "stable_audio_tools.models.diffusion", "stable_audio_tools.models.dit",
                                    "stable_audio_tools.models.utils", "stable_audio_tools.inference.generation"],
        "audioldm-s-full-v2": ["numpy", "scipy", "torch", "transformers", "diffusers"],
    }
    versions = {}
    for name in stacks.get(model_id, []):
        try:
            module = importlib.import_module(name)
        except Exception as e:  # ImportError, the NumPy ABI ValueError, ...
            fail("runtime", "%s\nWhile importing %s: %s: %s" % (RUNTIME_HELP, name, type(e).__name__, e), 5)
        if "." not in name:
            versions[name] = getattr(module, "__version__", "?")
    numpy_version = versions.get("numpy", "")
    if numpy_version.startswith("2.") and model_id == "stable-audio-open-small":
        try:
            import pywt
            if tuple(int(x) for x in pywt.__version__.split(".")[:2]) < (1, 6):
                fail("runtime", "%s\nNumPy %s is installed but PyWavelets %s needs NumPy 1.x." % (RUNTIME_HELP, numpy_version, pywt.__version__), 5)
        except SystemExit:
            raise
        except Exception:
            pass
    return versions


def import_torch():
    try:
        import torch  # noqa: F401
        return torch
    except Exception as e:  # pragma: no cover
        fail("runtime", "The Python runtime has no torch (%s). Run dooblr-generate/scripts/setup-runtime.sh." % e, 5)


def generate_test_backend(args, out):
    """NOT AI. A deterministic, seeded synthetic burst used by the tests and for checking the
    plug-in without a model. Every result is labelled as a test in the plug-in."""
    sr = 44100
    n = max(1, int(args.seconds * sr))
    rng = random.Random(args.seed)
    tone = 80.0 + sum(map(ord, args.prompt)) % 400
    left, right = [], []
    for i in range(n):
        t = i / sr
        env = math.exp(-t * 18.0)
        v = env * (0.6 * math.sin(2 * math.pi * tone * t) + 0.4 * (rng.random() * 2 - 1))
        left.append(v)
        right.append(v * 0.9)
        if i % 22050 == 0:
            progress("sampling", i / n)
    progress("writing", 0.95)
    write_wav(out, [left, right], sr)
    return {"sample_rate": sr, "channels": 2, "device": "cpu", "load_seconds": 0.0}


def generate_audioldm(args, out, model_dir):
    torch = import_torch()
    try:
        from diffusers import AudioLDMPipeline
    except Exception as e:
        fail("runtime", "The Python runtime has no diffusers (%s). Run dooblr-generate/scripts/setup-runtime.sh." % e, 5)
    device = pick_device(torch)
    progress("loading", 0.02, "loading AudioLDM on " + device)
    t0 = time.time()
    pipe = AudioLDMPipeline.from_pretrained(model_dir, torch_dtype=torch.float32, local_files_only=True)
    pipe = pipe.to(device)
    pipe.set_progress_bar_config(disable=True)
    load_s = time.time() - t0
    steps = args.steps or MODELS["audioldm-s-full-v2"]["steps"]

    def cb(step, _t, _latents):
        progress("sampling", 0.1 + 0.8 * (step + 1) / steps)

    gen = torch.Generator("cpu").manual_seed(int(args.seed))
    progress("sampling", 0.1)
    # AudioLDM's length must cover whole vocoder hops; ask for a little more and trim.
    length = max(1.0, math.ceil(args.seconds / 1.024) * 1.024)
    result = pipe(args.prompt, num_inference_steps=steps, audio_length_in_s=length, generator=gen,
                  callback=cb, callback_steps=1)
    audio = result.audios[0]
    sr = 16000
    audio = audio[: int(args.seconds * sr)]
    progress("writing", 0.95)
    write_wav_numpy(out, audio, sr)
    return {"sample_rate": sr, "channels": 1, "device": device, "load_seconds": round(load_s, 3), "steps": steps}


def load_init_audio(path, torch):
    """Loads a WAV the plug-in stored (44.1 kHz stereo 24-bit) as (sample_rate, tensor [ch, n])."""
    try:
        import torchaudio
        wav, sr = torchaudio.load(path)
        return int(sr), wav
    except Exception:
        pass
    import numpy as np
    with wave.open(path, "rb") as w:
        sr = w.getframerate()
        nch = w.getnchannels()
        sw = w.getsampwidth()
        n = w.getnframes()
        raw = w.readframes(n)
    if sw == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        ints = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        ints = np.where(ints >= 0x800000, ints - 0x1000000, ints)
        audio = ints.astype(np.float32) / 8388607.0
    elif sw == 2:
        audio = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32767.0
    else:
        fail("args", "unsupported source WAV sample width %d" % sw, 2)
    if nch > 1:
        audio = audio.reshape(-1, nch).T
    else:
        audio = audio.reshape(1, -1)
    return int(sr), torch.from_numpy(np.ascontiguousarray(audio))


def generate_stable_audio(args, out, model_dir):
    torch = import_torch()
    try:
        from stable_audio_tools.models.factory import create_model_from_config
        from stable_audio_tools.models.utils import load_ckpt_state_dict
        from stable_audio_tools.inference.generation import generate_diffusion_cond
    except Exception as e:
        fail("runtime", "The Python runtime has no stable-audio-tools (%s). Run dooblr-generate/scripts/setup-runtime.sh." % e, 5)
    device = pick_device(torch)
    progress("loading", 0.02, "loading Stable Audio Open Small on " + device)
    t0 = time.time()
    with open(os.path.join(model_dir, "model_config.json"), "r", encoding="utf-8") as f:
        config = json.load(f)
    model = create_model_from_config(config)
    model.load_state_dict(load_ckpt_state_dict(os.path.join(model_dir, "model.safetensors")))
    model = model.to(device).eval()
    load_s = time.time() - t0
    sr = int(config["sample_rate"])
    steps = args.steps or MODELS["stable-audio-open-small"]["steps"]
    init_audio = None
    if args.init_audio:
        init_audio = load_init_audio(args.init_audio, torch)
    progress("sampling", 0.15)
    kwargs = dict(
        steps=steps, cfg_scale=1.0,
        conditioning=[{"prompt": args.prompt, "seconds_total": float(args.seconds)}],
        sample_size=int(config["sample_size"]), sample_rate=sr, seed=int(args.seed) % (2 ** 32 - 1),
        sampler_type="pingpong", device=device)
    if init_audio is not None:
        kwargs["init_audio"] = init_audio
        kwargs["init_noise_level"] = float(args.init_noise)
    with torch.no_grad():
        audio = generate_diffusion_cond(model, **kwargs)
    progress("decoding", 0.9)
    audio = audio[0].to(torch.float32).cpu().numpy()[:, : int(args.seconds * sr)]
    progress("writing", 0.95)
    write_wav_numpy(out, audio, sr)
    return {"sample_rate": sr, "channels": int(audio.shape[0]), "device": device,
            "load_seconds": round(load_s, 3), "steps": steps}


def cmd_generate(args):
    model = MODELS.get(args.model)
    if model is None:
        fail("args", "unknown model " + args.model, 2)
    args.init_audio = (args.init_audio or "").strip()
    if not args.prompt.strip() and not args.init_audio:
        fail("args", "the prompt is empty", 2)
    if args.init_audio and args.model == "audioldm-s-full-v2":
        fail("args", "AudioLDM S v2 has no audio-to-audio path in this runtime. "
             "Source variation is only available with Stable Audio Open Small.", 2)
    if args.init_audio and not os.path.isfile(args.init_audio):
        fail("args", "source sample not found: " + args.init_audio, 2)
    args.seconds = max(0.1, min(float(args.seconds), model["max_seconds"]))
    if args.model != "test-backend":
        missing = missing_files(args.model, args.model_dir)
        if missing:
            fail("missing", "The model is not installed in %s (missing: %s)." % (args.model_dir, ", ".join(missing)), 3)
        os.environ.setdefault("HF_HOME", args.hf_home or os.path.join(os.path.dirname(args.model_dir.rstrip("/")), "hf-cache"))
        os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")
        os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    progress("loading", 0.01, "checking the Python runtime")
    runtime_self_check(args.model)
    t0 = time.time()
    try:
        if args.model == "test-backend":
            info = generate_test_backend(args, args.out)
        elif args.model == "audioldm-s-full-v2":
            info = generate_audioldm(args, args.out, args.model_dir)
        else:
            info = generate_stable_audio(args, args.out, args.model_dir)
    except SystemExit:
        raise
    except Exception as e:
        fail("generate", "%s: %s\n%s" % (type(e).__name__, e, traceback.format_exc(limit=4)), 6)
    total = time.time() - t0
    info.update({"wav": os.path.abspath(args.out), "model": args.model, "seed": int(args.seed),
                 "seconds": args.seconds, "prompt": args.prompt, "total_seconds": round(total, 3),
                 "generate_seconds": round(total - info.get("load_seconds", 0.0), 3),
                 "is_test": args.model == "test-backend"})
    progress("writing", 1.0)
    emit("done", **info)


def dir_size(path):
    total = 0
    for root, _dirs, files in os.walk(path):
        for f in files:
            try:
                total += os.path.getsize(os.path.join(root, f))
            except OSError:
                pass
    return total


def cmd_download(args):
    model = MODELS.get(args.model)
    if model is None or model["repo"] is None:
        fail("args", "nothing to download for " + args.model, 2)
    try:
        from huggingface_hub import HfApi, snapshot_download
        from huggingface_hub.utils import GatedRepoError, RepositoryNotFoundError, HfHubHTTPError
    except Exception as e:
        fail("runtime", "The Python runtime has no huggingface_hub (%s). Run dooblr-generate/scripts/setup-runtime.sh." % e, 5)
    token = read_token(args.token_file)
    if model["gated"] and not token:
        fail("access", ACCESS_HELP, 4)
    os.makedirs(args.model_dir, exist_ok=True)
    import fnmatch
    expected = 0
    try:
        info = HfApi().model_info(model["repo"], files_metadata=True, token=token or False)
        for s in info.siblings or []:
            if any(fnmatch.fnmatch(s.rfilename, p) for p in model["allow"]) and s.size:
                expected += s.size
    except Exception:
        expected = 0
    done = threading.Event()
    start_size = 0

    def report():
        while not done.wait(0.5):
            size = dir_size(args.model_dir) - start_size
            frac = size / expected if expected else 0.0
            progress("downloading", min(0.99, frac), "%.0f / %.0f MB" % (size / 1e6, expected / 1e6))

    progress("downloading", 0.0, "%.0f MB" % (expected / 1e6))
    t = threading.Thread(target=report, daemon=True)
    t.start()
    t0 = time.time()
    try:
        snapshot_download(model["repo"], local_dir=args.model_dir, allow_patterns=model["allow"], token=token or False)
    except GatedRepoError:
        done.set()
        fail("access", ACCESS_HELP + " (Hugging Face refused this token: the licence may not be accepted yet.)", 4)
    except RepositoryNotFoundError as e:
        done.set()
        fail("access", "Hugging Face refused the request (%s). %s" % (e, ACCESS_HELP), 4)
    except HfHubHTTPError as e:
        done.set()
        fail("download", "Download failed: %s" % e, 7)
    except Exception as e:
        done.set()
        fail("download", "Download failed: %s: %s" % (type(e).__name__, e), 7)
    done.set()
    missing = missing_files(args.model, args.model_dir)
    if missing:
        fail("download", "Download finished but files are missing: " + ", ".join(missing), 7)
    progress("downloading", 1.0)
    emit("done", model=args.model, model_dir=args.model_dir, bytes=dir_size(args.model_dir),
         seconds=round(time.time() - t0, 2))


def cmd_check(args):
    if args.model not in MODELS:
        fail("args", "unknown model " + args.model, 2)
    missing = [] if args.model == "test-backend" else missing_files(args.model, args.model_dir)
    if missing:
        fail("missing", "missing: " + ", ".join(missing), 3)
    emit("done", model=args.model, ready=True)


def cmd_check_runtime(args):
    versions = {}
    for model_id in ([args.model] if args.model else ["stable-audio-open-small", "audioldm-s-full-v2"]):
        versions.update(runtime_self_check(model_id))
    emit("done", python=sys.version.split()[0], versions=versions)


def cmd_env(_args):
    info = {"python": sys.version.split()[0], "worker": WORKER_VERSION}
    try:
        import torch
        info["torch"] = torch.__version__
        info["device"] = pick_device(torch)
    except Exception as e:
        info["torch"] = None
        info["torch_error"] = str(e)
    emit("done", **info)


def main(argv=None):
    p = argparse.ArgumentParser(description="dooblr-generate worker")
    sub = p.add_subparsers(dest="command", required=True)
    g = sub.add_parser("generate")
    g.add_argument("--model", required=True)
    g.add_argument("--model-dir", default="")
    g.add_argument("--hf-home", default="")
    g.add_argument("--prompt", required=True)
    g.add_argument("--seed", type=int, default=0)
    g.add_argument("--seconds", type=float, default=2.0)
    g.add_argument("--steps", type=int, default=0)
    g.add_argument("--out", required=True)
    g.add_argument("--init-audio", default="", help="WAV path for audio-to-audio variation (Stable Audio Open Small)")
    g.add_argument("--init-noise", type=float, default=1.0, help="init_noise_level, 0=closest to source, 1=furthest")
    d = sub.add_parser("download")
    d.add_argument("--model", required=True)
    d.add_argument("--model-dir", required=True)
    d.add_argument("--token-file", default="")
    c = sub.add_parser("check")
    c.add_argument("--model", required=True)
    c.add_argument("--model-dir", default="")
    r = sub.add_parser("check-runtime")
    r.add_argument("--model", default="")
    sub.add_parser("env")
    args = p.parse_args(argv)
    {"generate": cmd_generate, "download": cmd_download, "check": cmd_check, "check-runtime": cmd_check_runtime,
     "env": cmd_env}[args.command](args)


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except BaseException as e:  # never die with an uncaught exception: report it and exit non-zero
        emit("error", code="internal", message="%s: %s" % (type(e).__name__, e))
        sys.exit(8)
