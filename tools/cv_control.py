# CV controls for Raster FPS: the webcam drives the game.
#
# The game (CV MODE in the main menu) starts this script; it can also be run by hand:
#   python tools/cv_control.py            (add --show to see the camera with the tracking drawn on it)
#
# Each camera frame goes through two MediaPipe models:
#   face landmarker      -> 478 face points + blendshapes: where the head is, how it's turned and tilted,
#                           how close it is, whether the mouth is open
#   gesture recognizer   -> one hand: Closed_Fist, Open_Palm, Thumb_Up, ILoveYou, ... and its 21 points,
#                           from which we see which fingers are up and whether the thumb is out (finger counts,
#                           the finger gun)
# and the results go to the game as one small JSON datagram on UDP 127.0.0.1:47888, about 30 times a
# second, with a 96x72 grey thumbnail of the camera so the game can show what it sees.
#
# All values are raw measurements; the game calibrates them against your neutral pose and turns them
# into peeking, looking, crouching, aiming and gestures (cvcontrols.cpp).
#
# The image is mirrored first, like a mirror: move or turn to your right and the numbers grow.
import argparse, base64, json, math, os, socket, sys, time, urllib.request

import cv2
import numpy as np
import mediapipe as mp
from mediapipe.tasks.python import BaseOptions
from mediapipe.tasks.python import vision

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL_DIR = os.path.join(HERE, 'cv_models')
MODELS = {
    'face_landmarker.task': 'https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task',
    'gesture_recognizer.task': 'https://storage.googleapis.com/mediapipe-models/gesture_recognizer/gesture_recognizer/float16/1/gesture_recognizer.task',
}
PORT = 47888
THUMB_W, THUMB_H = 96, 72

# face mesh points used
NOSE, CHIN, FOREHEAD = 1, 152, 10
CHEEK_A, CHEEK_B = 234, 454
EYE_A, EYE_B = 33, 263


def model_path(name):
    """The model file, downloaded once into tools/cv_models/."""
    os.makedirs(MODEL_DIR, exist_ok=True)
    path = os.path.join(MODEL_DIR, name)
    if not os.path.exists(path):
        print('downloading', name, file=sys.stderr)
        urllib.request.urlretrieve(MODELS[name], path + '.part')
        os.replace(path + '.part', path)
    return path


def finger_count(wl):
    """Which fingers are up (index, middle, ring, little as '1'/'0') and whether the thumb sticks out.

    Uses the hand's 3D points in metres (MediaPipe hand_world_landmarks), not the flat image, so it
    works however the hand is turned: a finger gun pointed at the camera, sideways or rotated 90 degrees.
    A finger is up when it is nearly straight: the bends at its two joints add up to under ~60 degrees."""
    P = [np.array([l.x, l.y, l.z]) for l in wl]
    def bend(a, b, c):                                           # angle between a->b and b->c, degrees
        u, v = P[b] - P[a], P[c] - P[b]
        cu = np.dot(u, v) / (np.linalg.norm(u) * np.linalg.norm(v) + 1e-9)
        return math.degrees(math.acos(max(-1.0, min(1.0, cu))))
    bends = [bend(mcp, pip, dip) + bend(pip, dip, tip) for mcp, pip, dip, tip in ((5, 6, 7, 8), (9, 10, 11, 12), (13, 14, 15, 16), (17, 18, 19, 20))]
    up = [b < 70.0 for b in bends]
    palm = np.linalg.norm(P[9] - P[0]) + 1e-9                    # wrist to the middle finger's base
    reach = np.linalg.norm(P[4] - P[5]) / palm                    # thumb tip away from the index knuckle
    thumb = reach > 0.5
    finger_count.last = (bends, reach)                            # for the log
    return ''.join('1' if u else '0' for u in up), bool(thumb)


def face_measures(lm, blend):
    """Head position, turn, tilt, size and mouth from the face points (0..1 image coordinates)."""
    p = lambda i: (lm[i].x, lm[i].y)
    nose, chin, fore = p(NOSE), p(CHIN), p(FOREHEAD)
    ca, cb = sorted([p(CHEEK_A), p(CHEEK_B)])            # left / right in the (mirrored) image
    ea, eb = sorted([p(EYE_A), p(EYE_B)])
    width = max(1e-3, cb[0] - ca[0])
    height = max(1e-3, chin[1] - fore[1])
    yaw = (nose[0] - (ca[0] + cb[0]) / 2) / width * 2          # turn: ~ -1 .. 1, + = to your right
    pitch = (nose[1] - (fore[1] + chin[1]) / 2) / height * 2   # nod: + = looking down
    roll = math.degrees(math.atan2(eb[1] - ea[1], eb[0] - ea[0]))   # tilt: + = right ear to right shoulder
    jaw = 0.0
    for c in blend or []:
        if c.category_name == 'jawOpen': jaw = c.score
    xs = [l.x for l in lm]; ys = [l.y for l in lm]
    return {'x': nose[0], 'y': nose[1], 'yaw': yaw, 'pitch': pitch, 'roll': roll, 'size': width, 'jaw': jaw,
            'box': [min(xs), min(ys), max(xs), max(ys)]}


def parent_alive(pid):
    """Is the game that started us still running? (Windows: a process handle that hasn't signalled)"""
    if not pid: return True
    if os.name == 'nt':
        import ctypes
        k = ctypes.windll.kernel32
        h = k.OpenProcess(0x00100000, False, pid)                # SYNCHRONIZE
        if not h: return False
        alive = k.WaitForSingleObject(h, 0) == 0x102             # WAIT_TIMEOUT: still running
        k.CloseHandle(h)
        return alive
    try: os.kill(pid, 0); return True
    except OSError: return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--camera', type=int, default=0)
    ap.add_argument('--port', type=int, default=PORT)
    ap.add_argument('--show', action='store_true', help='show the camera with the tracking drawn on it')
    ap.add_argument('--parent', type=int, default=0, help='quit when this process (the game) is gone')
    args = ap.parse_args()

    face = vision.FaceLandmarker.create_from_options(vision.FaceLandmarkerOptions(
        base_options=BaseOptions(model_asset_path=model_path('face_landmarker.task')),
        running_mode=vision.RunningMode.VIDEO, num_faces=1, output_face_blendshapes=True))
    hands = vision.GestureRecognizer.create_from_options(vision.GestureRecognizerOptions(
        base_options=BaseOptions(model_asset_path=model_path('gesture_recognizer.task')),
        running_mode=vision.RunningMode.VIDEO, num_hands=1))

    cap = cv2.VideoCapture(args.camera, cv2.CAP_DSHOW if os.name == 'nt' else cv2.CAP_ANY)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640); cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
    if not cap.isOpened():
        cap = cv2.VideoCapture(args.camera)                      # the default backend
    if not cap.isOpened():
        print('no camera', file=sys.stderr); sys.exit(2)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    t0 = time.monotonic(); last_ts = -1; misses = 0
    print('cv_control: sending to 127.0.0.1:%d' % args.port, file=sys.stderr)
    checked = time.monotonic()
    log = open(os.path.join(HERE, 'cv_hand_log.txt'), 'w')       # every hand reading, for tuning the signs
    log.write('time_ms  gesture score  bend(index middle ring little) deg  thumb_reach  fingers thumb_out\n')
    while True:
        if time.monotonic() - checked > 1.0:                     # the game closed (or crashed): free the camera
            checked = time.monotonic()
            if not parent_alive(args.parent): break
        ok, frame = cap.read()
        if not ok:                                                   # busy in another app, or unplugged: tell the game
            misses += 1
            if misses % 20 == 0: sock.sendto(b'{"status":"nocam"}', ('127.0.0.1', args.port))
            time.sleep(0.05); continue
        misses = 0
        frame = cv2.flip(frame, 1)                                   # a mirror
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        img = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
        ts = int((time.monotonic() - t0) * 1000)
        if ts <= last_ts: ts = last_ts + 1
        last_ts = ts
        fr = face.detect_for_video(img, ts)
        hr = hands.recognize_for_video(img, ts)

        msg = {'t': ts, 'face': 0, 'hand': 0, 'gesture': 'None', 'gscore': 0.0}
        if fr.face_landmarks:
            msg.update(face_measures(fr.face_landmarks[0], fr.face_blendshapes[0] if fr.face_blendshapes else None))
            msg['face'] = 1
        if hr.gestures and hr.gestures[0]:
            g = hr.gestures[0][0]
            msg.update(hand=1, gesture=g.category_name or 'None', gscore=round(float(g.score), 3))
            hl = hr.hand_landmarks[0]
            msg['hx'] = sum(l.x for l in hl) / len(hl); msg['hy'] = sum(l.y for l in hl) / len(hl)
            wl = hr.hand_world_landmarks[0] if hr.hand_world_landmarks else hl
            msg['fbits'], msg['thumbout'] = finger_count(wl)
            b, reach = finger_count.last
            log.write('%7d  %-12s %.2f  %5.0f %5.0f %5.0f %5.0f   %.2f   %s %s\n' % (ts, msg['gesture'], msg['gscore'], b[0], b[1], b[2], b[3], reach, msg['fbits'], msg['thumbout']))
            log.flush()
        thumb = cv2.resize(cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY), (THUMB_W, THUMB_H), interpolation=cv2.INTER_AREA)
        msg['thumb'] = base64.b64encode(thumb.tobytes()).decode('ascii')
        sock.sendto(json.dumps(msg, separators=(',', ':')).encode(), ('127.0.0.1', args.port))

        if args.show:
            h, w = frame.shape[:2]
            if msg['face']:
                b = msg['box']; cv2.rectangle(frame, (int(b[0] * w), int(b[1] * h)), (int(b[2] * w), int(b[3] * h)), (0, 220, 0), 2)
                cv2.putText(frame, 'yaw %+.2f pitch %+.2f roll %+.0f size %.2f jaw %.2f' % (msg['yaw'], msg['pitch'], msg['roll'], msg['size'], msg['jaw']),
                            (10, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 0), 2)
            if msg['hand']:
                cv2.putText(frame, '%s %.2f  fingers %s thumb %s' % (msg['gesture'], msg['gscore'], msg.get('fbits', '----'), msg.get('thumbout')),
                            (10, 50), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 200, 255), 2)
            cv2.imshow('Raster FPS - CV controls (Esc closes)', frame)
            if cv2.waitKey(1) == 27: break


    cap.release()


if __name__ == '__main__':
    main()
