# frozen_string_literal: true

require "openvfs/occ"
require "socket"
require "timeout"
require "tmpdir"

class UploadServer
  attr_reader :requests, :files

  def initialize
    @socket = TCPServer.new("127.0.0.1", 0)
    @requests = []
    @files = {}
    @stall = Queue.new
    @thread = Thread.new do
      loop do
        connection = @socket.accept
        begin
          serve(connection)
        rescue Errno::EPIPE, Errno::ECONNRESET
          # Cancellation may close the peer before the response is sent.
        ensure
          connection.close
        end
      end
    end
  end

  def url = "http://127.0.0.1:#{@socket.addr[1]}/dav/space"

  def close
    @thread.kill
    @thread.join
    @socket.close
  end

  def serve(connection)
    request_line = connection.gets("\r\n")
    if request_line
      serve_request(connection, request_line)
    end
  end

  def serve_request(connection, request_line)
    method, path, = request_line.split(" ", 3)
    headers = {}
    while (line = connection.gets("\r\n")) && line != "\r\n"
      name, value = line.split(":", 2)
      headers[name.downcase] = value.strip
    end

    if headers["expect"] == "100-continue"
      connection.write("HTTP/1.1 100 Continue\r\n\r\n")
    end

    body = connection.read(Integer(headers.fetch("content-length", "0")))
    @requests << {method: method, path: path, headers: headers, body: body}

    if path.end_with?("/stall")
      @stall.pop
    elsif path.end_with?("/drop")
      nil
    elsif headers["authorization"] != "Bearer test-token"
      respond(connection, 401, "Unauthorized")
    elsif path.end_with?("/quota")
      respond(connection, 507, "Insufficient Storage", "<error><message>Quota exceeded</message></error>")
    elsif path.end_with?("/accepted")
      respond(connection, 202, "Accepted")
    elsif headers["if-none-match"] == "*" && @files.key?(path)
      respond(connection, 412, "Precondition Failed")
    elsif headers.key?("if-match") && headers["if-match"] != @files.dig(path, :etag)
      respond(connection, 412, "Precondition Failed")
    else
      previous = @files[path]
      revision = @files.fetch(path, {}).fetch(:revision, 0) + 1
      etag = "\"revision-#{revision}\""
      @files[path] = {body: body, etag: etag, revision: revision}
      if previous
        respond(connection, 204, "No Content", "", {"ETag" => etag, "OC-FileID" => "file-123"})
      else
        respond(connection, 201, "Created", "", {"ETag" => etag, "OC-FileID" => "file-123"})
      end
    end
  end

  def respond(connection, status, reason, body = "", headers = {})
    connection.write("HTTP/1.1 #{status} #{reason}\r\nContent-Length: #{body.bytesize}\r\nConnection: close\r\n")
    headers.each do |name, value|
      connection.write("#{name}: #{value}\r\n")
    end
    connection.write("\r\n#{body}")
  end
end

describe OpenVFS::OCC do
  around do |&test|
    Dir.mktmpdir("openvfs-upload-") do |directory|
      @path = File.join(directory, "upload.bin")
      File.binwrite(@path, "original\x00\xff".b)
      @server = UploadServer.new
      begin
        @account = OpenVFS::OCC::Account.new(@server.url, "test-token")
        super(&test)
      ensure
        @server.close
      end
    end
  end

  it "streams a new binary file with desktop authentication and metadata headers" do
    data = ("hello\x00\xff".b * 160_000)
    File.binwrite(@path, data)
    result = @account.upload_new(@path, "report.bin")

    expect(result.ok?).to be == true
    expect(result.http_status).to be == 201
    expect(result.etag).to be == "revision-1"
    expect(result.file_id).to be == "file-123"
    expect(result.total_bytes).to be == data.bytesize
    expect(result.request_id.empty?).to be == false
    expect(@server.files.fetch("/dav/space/report.bin").fetch(:body)).to be == data

    request = @server.requests.first
    expect(request.fetch(:method)).to be == "PUT"
    expect(request.fetch(:headers).fetch("authorization")).to be == "Bearer test-token"
    expect(request.fetch(:headers).fetch("if-none-match")).to be == "*"
    expect(request.fetch(:headers).fetch("oc-total-length")).to be == data.bytesize.to_s
    expect(request.fetch(:headers).fetch("x-oc-mtime")).to be == File.mtime(@path).to_i.to_s
    expect(request.fetch(:headers).fetch("user-agent").include?("mirall/")).to be == true
  end

  it "uploads changed contents against the previous remote ETag" do
    first = @account.upload_new(@path, "report.bin")
    File.binwrite(@path, "changed content")
    second = @account.upload_changed(@path, "report.bin", etag: first.etag)

    expect(second.ok?).to be == true
    expect(second.http_status).to be == 204
    expect(second.etag).to be == "revision-2"
    expect(@server.files.fetch("/dav/space/report.bin").fetch(:body)).to be == "changed content"
    expect(@server.requests.last.fetch(:headers).fetch("if-match")).to be == "\"revision-1\""
  end

  it "reports a stale revision without overwriting the remote file" do
    first = @account.upload_new(@path, "report.bin")
    File.binwrite(@path, "second revision")
    @account.upload_changed(@path, "report.bin", etag: first.etag)
    File.binwrite(@path, "stale edit")
    conflict = @account.upload_changed(@path, "report.bin", etag: first.etag)

    expect(conflict.ok?).to be == false
    expect(conflict.http_status).to be == 412
    expect(conflict.error.empty?).to be == false
    expect(@server.files.fetch("/dav/space/report.bin").fetch(:body)).to be == "second revision"
  end

  it "refuses to replace an existing file through upload_new" do
    @account.upload_new(@path, "report.bin")
    File.binwrite(@path, "accidental replacement")
    result = @account.upload_new(@path, "report.bin")

    expect(result.ok?).to be == false
    expect(result.http_status).to be == 412
    expect(@server.files.fetch("/dav/space/report.bin").fetch(:body)).to be == "original\x00\xff".b
  end

  it "uploads an empty file and encodes spaces and Unicode in its path" do
    File.binwrite(@path, "")
    result = @account.upload_new(@path, "résumé empty.txt")

    expect(result.ok?).to be == true
    expect(result.total_bytes).to be == 0
    expect(@server.requests.first.fetch(:path)).to be == "/dav/space/r%C3%A9sum%C3%A9%20empty.txt"
    expect(@server.requests.first.fetch(:body)).to be == ""
  end

  it "preserves server error details in the result" do
    result = @account.upload_new(@path, "quota")

    expect(result.ok?).to be == false
    expect(result.http_status).to be == 507
    expect(result.error.include?("Quota exceeded")).to be == true
    expect(result.response_body).to be == "<error><message>Quota exceeded</message></error>"
  end

  it "reports authentication failures and accepts replacement credentials" do
    @account.access_token = "expired-token"
    failure = @account.upload_new(@path, "report.bin")
    expect(failure.ok?).to be == false
    expect(failure.http_status).to be == 401

    @account.access_token = "test-token"
    expect(@account.upload_new(@path, "report.bin").ok?).to be == true
  end

  it "does not treat an accepted but unfinished upload as successful" do
    result = @account.upload_new(@path, "accepted")
    expect(result.ok?).to be == false
    expect(result.http_status).to be == 202
  end

  it "reports a dropped connection" do
    result = @account.upload_new(@path, "drop", timeout_ms: 1000)
    expect(result.ok?).to be == false
    expect(result.network_error).to be > 0
  end

  it "times out a stalled response while allowing Ruby threads to run" do
    result = @account.upload_new(@path, "stall", timeout_ms: 100)

    expect(result.ok?).to be == false
    expect(result.timed_out?).to be == true
    expect(result.error).to be == "Upload timed out"
    expect(result.elapsed_ms).to be < 5000
  end

  it "keeps native account and result data alive across garbage collection" do
    job = OpenVFS::OCC::PUTFileJob.new(@account, @path, "report.bin", nil, true, 1000)
    @account = nil
    GC.start
    expect(job.result).to be_nil

    job.start
    result = job.wait
    GC.start
    expect(job.finished?).to be == true
    expect(job.result.etag).to be == result.etag
    job = nil
    GC.start
    expect(result.file_id).to be == "file-123"
  end

  it "cancels a running upload and preserves its terminal result" do
    job = OpenVFS::OCC::PUTFileJob.new(@account, @path, "stall", nil, true, 1000)
    job.start
    job.abort
    result = job.wait
    expect(result.ok?).to be == false
    expect(result.aborted?).to be == true
    expect(result.error).to be == "Upload aborted"
  end

  it "rejects invalid job lifecycle and inputs before a request is sent" do
    job = OpenVFS::OCC::PUTFileJob.new(@account, @path, "report.bin")
    expect {job.wait}.to raise_exception(RuntimeError)
    expect {@account.upload_new("#{@path}.missing", "report.bin")}.to raise_exception(ArgumentError)
    expect {@account.upload_changed(@path, "report.bin", etag: nil)}.to raise_exception(ArgumentError)
    expect {OpenVFS::OCC::PUTFileJob.new(@account, @path, "../escape")}.to raise_exception(ArgumentError)
    expect(@server.requests).to be == []
  end

  it "aborts the native upload when Ruby interrupts a wait" do
    job = OpenVFS::OCC::PUTFileJob.new(@account, @path, "stall", nil, true, 5000)
    job.start
    expect {Timeout.timeout(0.1) {job.wait}}.to raise_exception(Timeout::Error)
    expect(job.wait.aborted?).to be == true
  end

  it "rejects cross-thread access to Qt objects" do
    error = Thread.new do
      begin
        @account.upload_new(@path, "report.bin")
      rescue RuntimeError => exception
        exception
      end
    end.value

    expect(error).to be_a(RuntimeError)
    expect(@server.requests).to be == []
  end
end
