package p2p

import (
	"encoding/json"
	"fmt"
	"net"
	"testing"
	"time"
)

func TestGoP2PLoopback(t *testing.T) {
	// Simple mock echo server in Go to test Go client
	l, err := net.Listen("tcp", "127.0.0.1:19993")
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()

	go func() {
		conn, err := l.Accept()
		if err != nil {
			return
		}
		defer conn.Close()

		for {
			msg, err := ReadFrame(conn)
			if err != nil {
				return
			}
			if msg.Type == "rpc_req" {
				res := &Message{
					Type:   "rpc_res",
					Id:     msg.Id,
					Result: "go_echo_success",
				}
				SendFrame(conn, res)
			}
		}
	}()

	client, err := Connect("tcp://127.0.0.1:19993")
	if err != nil {
		t.Fatal(err)
	}
	defer client.Close()

	res, err := client.Call("echo", map[string]string{"msg": "hi"}, 2*time.Second)
	if err != nil {
		t.Fatal(err)
	}
	if fmt.Sprintf("%v", res) != "go_echo_success" {
		t.Fatalf("unexpected result: %v", res)
	}
	_ = json.Marshal
}
