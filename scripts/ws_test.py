import paho.mqtt.client as mqtt, time
try:
    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, transport="websockets")
except Exception:
    c = mqtt.Client(transport="websockets")
got = []
def on_c(cl, u, f, rc, *a):
    print("WS CONNECT rc =", rc); cl.subscribe("jamshield/feed")
def on_m(cl, u, m):
    got.append(1)
    if len(got) <= 3: print("MSG", m.payload[:90].decode("utf-8", "ignore"))
c.on_connect = on_c; c.on_message = on_m
c.connect("127.0.0.1", 9001, 30)
c.loop_start(); time.sleep(6); c.loop_stop()
print("messages over WS:", len(got))
