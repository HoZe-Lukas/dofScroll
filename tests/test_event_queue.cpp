// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#undef NDEBUG
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

#include "platform/event_queue.hpp"

using namespace spn;

static InputEvent motion(int x) {
    InputEvent e;
    e.axes[0] = x;
    return e;
}
static InputEvent button(int b, bool down) {
    InputEvent e;
    e.type = InputEventType::Button;
    e.button = b;
    e.down = down;
    return e;
}

int main() {
    EventQueue q(4);
    InputEvent e;
    assert(q.wait(&e, 0) == WaitResult::Timeout);
    assert(q.wait(&e, 10) == WaitResult::Timeout);

    q.push(motion(1));
    q.push(button(2, true));
    assert(q.wait(&e, 0) == WaitResult::Event && e.type == InputEventType::Motion && e.axes[0] == 1);
    assert(q.wait(&e, 0) == WaitResult::Event && e.type == InputEventType::Button && e.button == 2);

    // overflow drops the oldest motion, never buttons
    q.push(button(1, true));
    q.push(motion(10));
    q.push(motion(11));
    q.push(motion(12));  // full (4)
    q.push(motion(13));  // drops motion(10)
    assert(q.size() == 4);
    assert(q.wait(&e, 0) == WaitResult::Event && e.type == InputEventType::Button);
    assert(q.wait(&e, 0) == WaitResult::Event && e.axes[0] == 11);
    assert(q.wait(&e, 0) == WaitResult::Event && e.axes[0] == 12);
    assert(q.wait(&e, 0) == WaitResult::Event && e.axes[0] == 13);

    // a blocked consumer wakes up when another thread pushes
    std::thread t([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        q.push(motion(42));
    });
    assert(q.wait(&e, 5000) == WaitResult::Event && e.axes[0] == 42);
    t.join();

    // close: drain first, then Closed; pushes after close are ignored; reopen works
    q.push(motion(5));
    q.close();
    q.push(motion(6));
    assert(q.wait(&e, 0) == WaitResult::Event && e.axes[0] == 5);
    assert(q.wait(&e, 0) == WaitResult::Closed);
    q.reopen();
    assert(q.wait(&e, 0) == WaitResult::Timeout);
    q.push(motion(7));
    assert(q.wait(&e, 0) == WaitResult::Event && e.axes[0] == 7);

    std::puts("test_event_queue: ok");
    return 0;
}
