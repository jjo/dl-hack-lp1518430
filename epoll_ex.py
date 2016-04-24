import select
import sys
import random


epoll = select.epoll()
epoll.register(sys.stdin.fileno(), select.EPOLLIN)

while True:
    timeout = random.random() * 0.10
    events = epoll.poll(timeout)
    timeout = random.random() * 0.10
    select.select([], [], [], timeout)
