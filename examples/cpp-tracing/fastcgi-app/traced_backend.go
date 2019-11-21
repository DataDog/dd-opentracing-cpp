package main

import (
	"log"
	"net/http"
	"time"

	httptrace "gopkg.in/DataDog/dd-trace-go.v1/contrib/net/http"
	"gopkg.in/DataDog/dd-trace-go.v1/ddtrace/tracer"
)

func main() {
	tracer.Start()
	defer tracer.Stop()

	// Configure http services
	mux := httptrace.NewServeMux(httptrace.WithServiceName("traced-backend"))
	mux.HandleFunc("/", rootHandler)

	log.Fatal(http.ListenAndServe(":80", mux))
}

func rootHandler(w http.ResponseWriter, r *http.Request) {
	// sleep for a bit of time, otherwise the span is barely visible in
	// trace flame view
	time.Sleep(2 * time.Millisecond)
	w.WriteHeader(http.StatusOK)
}
