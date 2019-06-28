import pytest

from ops import Push, Pop, Broadcast, KEY_CKS_DATA, KEY_CKS_CONTROL, KEY_BROADCAST, KEY_CKR_DATA
from program import Program, FailedAllocation


def test_allocation_fail():
    with pytest.raises(FailedAllocation):
        Program(4096, [
            Push(0),
            Broadcast(0)
        ])


def test_allocation_overlap():
    program = Program(4096, [
        Push(0),
        Broadcast(1)
    ])

    group = program.create_group(KEY_CKS_DATA)
    assert group.hw_port_count == 2
    assert group.hw_mapping() == [0, 1]

    group = program.create_group(KEY_CKS_CONTROL)
    assert group.hw_port_count == 1
    assert group.hw_mapping() == [-1, 0]

    group = program.create_group(KEY_BROADCAST)
    assert group.hw_port_count == 1
    assert group.hw_mapping() == [-1, 0]


def test_allocation_hw_port():
    program = Program(4096, [
        Push(0),
        Push(1),
        Pop(2),
        Broadcast(3)
    ])

    assert program.create_group(KEY_CKS_DATA).get_hw_port(0) == 0
    assert program.create_group(KEY_CKS_DATA).get_hw_port(1) == 1
    assert program.create_group(KEY_CKR_DATA).get_hw_port(2) == 0
    assert program.create_group(KEY_BROADCAST).get_hw_port(0) == -1
    assert program.create_group(KEY_BROADCAST).get_hw_port(3) == 0


def test_allocation_channel_to_ports():
    program = Program(4096, [
        Push(0),
        Pop(0),
        Push(1),
        Push(2),
        Pop(2)
    ])

    assert program.get_channel_allocations(0) == {
        "cks": [("data", 0, 0), ("control", 2, 1)],
        "ckr": [("data", 0, 0), ("control", 2, 2)]
    }
    assert program.get_channel_allocations(1) == {
        "cks": [("data", 1, 1)],
        "ckr": [("data", 2, 1)]
    }
    assert program.get_channel_allocations(2) == {
        "cks": [("data", 2, 2)],
        "ckr": [("control", 0, 0)]
    }
    assert program.get_channel_allocations(3) == {
        "cks": [("control", 0, 0)],
        "ckr": [("control", 1, 1)]
    }


def test_allocation_get_channel():
    program = Program(4096, [
        Push(0),
        Pop(0),
        Push(1),
        Push(2),
        Pop(2)
    ])

    assert program.get_channel_for_logical_port(0, KEY_CKS_DATA) == 0
    assert program.get_channel_for_logical_port(0, KEY_CKS_CONTROL) == 3
    assert program.get_channel_for_logical_port(1, KEY_CKR_DATA) is None
    assert program.get_channel_for_logical_port(2, KEY_CKS_DATA) == 2
