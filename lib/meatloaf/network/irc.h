// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

// IRC:// - Internet Relay Chat Protocol for real-time chat and messaging
// https://en.wikipedia.org/wiki/Internet_Relay_Chat
// https://tools.ietf.org/html/rfc1459
//

#ifndef MEATLOAF_SCHEME_IRC
#define MEATLOAF_SCHEME_IRC

#include "meatloaf.h"
#include "meat_session.h"
#include "network/tcp.h"


/********************************************************
 * MSession - IRC Session Management
 ********************************************************/

class IRCMSession : public MSession {
public:
	IRCMSession(std::string host, uint16_t port = 6667)
		: MSession("irc://" + host + ":" + std::to_string(port), host, port)
	{
		parseHost();
		Debug_printv("IRCMSession created for %s:%d", host.c_str(), port);
	}
	~IRCMSession() override {
		Debug_printv("IRCMSession destroyed for %s:%d", host.c_str(), port);
		disconnect();
	}

	bool connect() override {
		if (connected) return true;

		if (server_host.empty()) {
			Debug_printv("IRCMSession connect failed: missing server host");
			return false;
		}

		std::string tcp_url = "tcp://" + server_host;
		if (port != 0 && port != 6667) {
			tcp_url += ":" + std::to_string(port);
		}

		_tcp = std::make_shared<TCPMStream>(tcp_url);
		if (!_tcp->open(std::ios_base::in | std::ios_base::out)) {
			Debug_printv("IRCMSession failed to open TCP stream to %s:%d", server_host.c_str(), port);
			_tcp.reset();
			connected = false;
			return false;
		}

		connected = true;
		updateActivity();
		return true;
	}

	void disconnect() override {
		if (!connected) return;
		if (_tcp) {
			_tcp->close();
			_tcp.reset();
		}
		connected = false;
	}

	bool keep_alive() override {
		if (!connected || !_tcp) return false;
		if (!_tcp->isOpen()) {
			connected = false;
			return false;
		}
		updateActivity();
		return true;
	}

	std::shared_ptr<TCPMStream> stream() { return _tcp; }

	const std::string& getNick() const { return nick; }
	const std::string& getServerHost() const { return server_host; }

	bool ensureRegistered() {
		if (!_tcp || registered) return registered;

		std::string reg_nick = nick.empty() ? "meatloaf" : nick;
		std::string nick_cmd = "NICK " + reg_nick + "\r\n";
		std::string user_cmd = "USER " + reg_nick + " 0 * :" + reg_nick + "\r\n";

		_tcp->write(reinterpret_cast<const uint8_t*>(nick_cmd.c_str()), nick_cmd.size());
		_tcp->write(reinterpret_cast<const uint8_t*>(user_cmd.c_str()), user_cmd.size());
		updateActivity();
		registered = true;
		return true;
	}

	bool ensureJoined(const std::string& channel) {
		if (channel.empty() || !_tcp) return false;
		if (!registered && !ensureRegistered()) return false;

		std::string join_channel = channel;
		if (!join_channel.empty() && join_channel[0] != '#' && join_channel[0] != '&') {
			join_channel = "#" + join_channel;
		}

		if (joined_channel == join_channel) return true;

		std::string join_cmd = "JOIN " + join_channel + "\r\n";
		_tcp->write(reinterpret_cast<const uint8_t*>(join_cmd.c_str()), join_cmd.size());
		updateActivity();
		joined_channel = join_channel;
		return true;
	}

private:
	void parseHost() {
		// host format: nick@server or server
		size_t at_pos = host.find('@');
		if (at_pos != std::string::npos) {
			nick = host.substr(0, at_pos);
			server_host = host.substr(at_pos + 1);
		} else {
			server_host = host;
		}
	}

	std::shared_ptr<TCPMStream> _tcp;
	std::string nick;
	std::string server_host;
	std::string joined_channel;
	bool registered = false;
};


/********************************************************
 * MStream - IRC Stream
 ********************************************************/

class IRCMStream : public MStream {
public:
	IRCMStream(std::string path) : MStream(path) {
	}
	~IRCMStream() override {
		close();
	}

	uint32_t size() override { return -1; }
	uint32_t available() override { return 0; }
	uint32_t position() override { return 0; }
	size_t error() override { return 0; }

	bool open(std::ios_base::openmode mode) override {
		if (isOpen()) return true;

		auto parser = PeoplesUrlParser::parseURL(url);
		if (!parser) {
			Debug_printv("IRCMStream: failed to parse URL [%s]", url.c_str());
			return false;
		}

		uint16_t irc_port = parser->port.empty() ? 6667 : std::stoi(parser->port);
		_session = SessionBroker::obtain<IRCMSession>(parser->host, irc_port);
		if (!_session) {
			Debug_printv("IRCMStream: failed to obtain session for %s:%d", parser->host.c_str(), irc_port);
			return false;
		}

		if (!_session->connect()) {
			Debug_printv("IRCMStream: failed to connect session for %s:%d", parser->host.c_str(), irc_port);
			_session.reset();
			return false;
		}

		std::string channel = parser->path;
		if (!channel.empty() && channel[0] == '/') {
			channel = channel.substr(1);
		}

		_session->ensureRegistered();
		if (!channel.empty()) {
			_session->ensureJoined(channel);
		}

		return true;
	}

	void close() override {
		if (_session) {
			_session->disconnect();
			_session.reset();
		}
	}

	uint32_t read(uint8_t* buf, uint32_t size) override {
		if (!isOpen() && !open(std::ios_base::in)) {
			return 0;
		}
		auto tcp = _session->stream();
		if (!tcp) return 0;
		return tcp->read(buf, size);
	}

	uint32_t write(const uint8_t *buf, uint32_t size) override {
		if (!isOpen() && !open(std::ios_base::out)) {
			return 0;
		}
		auto tcp = _session->stream();
		if (!tcp) return 0;
		return tcp->write(buf, size);
	}

	bool seek(uint32_t pos) override { return false; }

	bool isOpen() override {
		return _session && _session->isConnected();
	}

protected:
	std::shared_ptr<IRCMSession> _session;
};


/********************************************************
 * MFile - IRC File
 ********************************************************/

class IRCMFile : public MFile {
public:
	IRCMFile() {
		Debug_printv("IRCMFile default constructor should not be used");
	}
	IRCMFile(std::string path) : MFile(path) {
		Debug_printv("IRCMFile created: url[%s]", url.c_str());
	}
	~IRCMFile() override = default;

	std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override {
		return createStream(mode);
	}

	std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src) override {
		return src;
	}

	std::shared_ptr<MStream> createStream(std::ios_base::openmode mode) override {
		return std::make_shared<IRCMStream>(url);
	}

	bool isDirectory() override { return false; }
};


/********************************************************
 * MFileSystem - IRC Filesystem
 ********************************************************/

class IRCMFileSystem : public MFileSystem {
public:
	IRCMFileSystem() : MFileSystem("irc") {
		isRootFS = true;
	}

	bool handles(std::string name) override {
		return mstr::startsWith(name, (char *)"irc:", false);
	}

	MFile* getFile(std::string path) override {
		return new IRCMFile(path);
	}
};


#endif /* MEATLOAF_SCHEME_IRC */