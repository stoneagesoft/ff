# Python 3 transcription of b3 (recursive fib(36)).
# Run: python3 b3.py
import sys
sys.setrecursionlimit(100000)

def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

fib(36)
