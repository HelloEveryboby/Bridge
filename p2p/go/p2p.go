package p2p

import (
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/url"
	"sync"
	"sync/atomic"
	"time"
)

var Magic = []byte("BRG1")
const MaxPayload = 16 * 1024 * 1024

type RpcHandler func(method string, params json.RawMessage) (interface{}, error)
type EventHandler func(topic string, data json.RawMessage)

type Message struct {
	Type   string          `json:"type"`
	Id     string          `json:"id,omitempty"`
	Method string          `json:"method,omitempty"`
	Params json.RawMessage `json:"params,omitempty"`
	Result interface{}     `json:"result,omitempty"`
	Error  interface{}     `json:"error,omitempty"`
	Topic  string          `json:"topic,omitempty"`
	Data   json.RawMessage `json:"data,omitempty"`
}

func SendFrame(conn net.Conn, msg *Message) error {
	data, err := json.Marshal(msg)
	if err != nil {
		return err
	}
	if len(data) > MaxPayload {
		return errors.New("payload too large")
	}
	header := make([]byte, 8)
	copy(header[:4], Magic)
	binary.BigEndian.PutUint32(header[4:], uint32(len(data)))

	if _, err := conn.Write(header); err != nil {
		return err
	}
	_, err = conn.Write(data)
	return err
}

func ReadFrame(conn net.Conn) (*Message, error) {
	header := make([]byte, 8)
	if _, err := io.ReadFull(conn, header); err != nil {
		return nil, err
	}
	if string(header[:4]) != string(Magic) {
		return nil, errors.New("invalid protocol magic")
	}
	payloadLen := binary.BigEndian.Uint32(header[4:])
	if payloadLen > MaxPayload {
		return nil, errors.New("payload exceeds maximum limit")
	}
	buf := make([]byte, payloadLen)
	if _, err := io.ReadFull(conn, buf); err != nil {
		return nil, err
	}
	var msg Message
	if err := json.Unmarshal(buf, &msg); err != nil {
		return nil, err
	}
	return &msg, nil
}

// Client
type Client struct {
	conn         net.Conn
	mu           sync.Mutex
	reqSeq       uint64
	pending      map[string]chan *Message
	eventHandlers map[string]EventHandler
	running      bool
}

func Connect(uriStr string) (*Client, error) {
	u, err := url.Parse(uriStr)
	if err != nil {
		return nil, err
	}
	var conn net.Conn
	if u.Scheme == "tcp" {
		conn, err = net.Dial("tcp", u.Host)
	} else if u.Scheme == "unix" {
		conn, err = net.Dial("unix", u.Path)
	} else {
		return nil, fmt.Errorf("unsupported scheme: %s", u.Scheme)
	}
	if err != nil {
		return nil, err
	}

	c := &Client{
		conn:          conn,
		pending:       make(map[string]chan *Message),
		eventHandlers: make(map[string]EventHandler),
		running:       true,
	}
	go c.readLoop()
	return c, nil
}

func (c *Client) readLoop() {
	for c.running {
		msg, err := ReadFrame(c.conn)
		if err != nil {
			break
		}
		if msg.Type == "rpc_res" {
			c.mu.Lock()
			ch, ok := c.pending[msg.Id]
			if ok {
				ch <- msg
			}
			c.mu.Unlock()
		} else if msg.Type == "event" {
			c.mu.Lock()
			h, ok := c.eventHandlers[msg.Topic]
			if !ok {
				h = c.eventHandlers["*"]
			}
			c.mu.Unlock()
			if h != nil {
				go h(msg.Topic, msg.Data)
			}
		}
	}
	c.mu.Lock()
	for _, ch := range c.pending {
		close(ch)
	}
	c.pending = make(map[string]chan *Message)
	c.mu.Unlock()
}

func (c *Client) Call(method string, params interface{}, timeout time.Duration) (interface{}, error) {
	seq := atomic.AddUint64(&c.reqSeq, 1)
	reqId := fmt.Sprintf("go-%d", seq)

	paramsBytes, err := json.Marshal(params)
	if err != nil {
		return nil, err
	}

	ch := make(chan *Message, 1)
	c.mu.Lock()
	c.pending[reqId] = ch
	c.mu.Unlock()

	req := &Message{
		Type:   "rpc_req",
		Id:     reqId,
		Method: method,
		Params: paramsBytes,
	}

	if err := SendFrame(c.conn, req); err != nil {
		c.mu.Lock()
		delete(c.pending, reqId)
		c.mu.Unlock()
		return nil, err
	}

	select {
	case res, ok := <-ch:
		c.mu.Lock()
		delete(c.pending, reqId)
		c.mu.Unlock()
		if !ok || res == nil {
			return nil, errors.New("connection closed")
		}
		if res.Error != nil {
			return nil, fmt.Errorf("rpc error: %v", res.Error)
		}
		return res.Result, nil
	case <-time.After(timeout):
		c.mu.Lock()
		delete(c.pending, reqId)
		c.mu.Unlock()
		return nil, errors.New("rpc call timeout")
	}
}

func (c *Client) Publish(topic string, data interface{}) error {
	b, err := json.Marshal(data)
	if err != nil {
		return err
	}
	msg := &Message{
		Type:  "event",
		Topic: topic,
		Data:  b,
	}
	return SendFrame(c.conn, msg)
}

func (c *Client) Subscribe(topic string, handler EventHandler) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.eventHandlers[topic] = handler
}

func (c *Client) Close() error {
	c.running = false
	return c.conn.Close()
}
