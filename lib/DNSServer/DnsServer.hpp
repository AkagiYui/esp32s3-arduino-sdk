#pragma once
#include <AsyncUDP.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <vector>

enum class DNSType : uint16_t {
    A = 1,      // IPv4
    AAAA = 28,  // IPv6
    CNAME = 5,  // Canonical name
    MX = 15,    // Mail exchange
    TXT = 16,   // Text records
    ANY = 255   // Any record type
};

struct DNSRecord {
    std::string domain;
    DNSType type;
    IPAddress ip;
    std::string data;  // For non-A records
    uint32_t ttl;
};

class AsyncDNSServer {
   private:
    static std::map<uint16_t, AsyncDNSServer> instances;
    static SemaphoreHandle_t instanceMutex;

    AsyncUDP _udp;
    uint16_t port;
    uint32_t defaultTTL;
    std::vector<DNSRecord> records;
    std::map<std::string, std::vector<std::string>> wildcardDomains;

    // DNS header structure
    struct DNSHeader {
        uint16_t id;
        uint16_t flags;
        uint16_t qdcount;
        uint16_t ancount;
        uint16_t nscount;
        uint16_t arcount;
    } __attribute__((packed));

    // DNS question structure
    struct DNSQuestion {
        const uint8_t* qname;
        size_t qnameLength;
        uint16_t qtype;
        uint16_t qclass;
    };

    // Private constructor for singleton pattern
    AsyncDNSServer(uint16_t port = 53) : port(port), defaultTTL(60) {
        if (instanceMutex == nullptr) {
            instanceMutex = xSemaphoreCreateMutex();
        }
    }

    void handleQuery(AsyncUDPPacket& packet) {
        if (packet.length() < sizeof(DNSHeader)) {
            return;
        }

        DNSHeader header;
        memcpy(&header, packet.data(), sizeof(DNSHeader));

        // Convert to host byte order
        header.id = ntohs(header.id);
        header.flags = ntohs(header.flags);
        header.qdcount = ntohs(header.qdcount);

        if ((header.flags & 0x8000) || header.qdcount != 1) {
            return;  // Not a query or multiple questions
        }

        DNSQuestion question;
        uint8_t* qname = (uint8_t*)(packet.data() + sizeof(DNSHeader));
        question.qname = qname;

        // Find end of qname
        size_t qnameLen = 0;
        while (qname[qnameLen] != 0) {
            qnameLen += qname[qnameLen] + 1;
            if (qnameLen >= packet.length() - sizeof(DNSHeader)) {
                return;  // Malformed packet
            }
        }
        question.qnameLength = qnameLen + 1;

        // Get qtype and qclass
        if (packet.length() < sizeof(DNSHeader) + qnameLen + 5) {
            return;  // Packet too short
        }

        memcpy(&question.qtype, qname + qnameLen + 1, sizeof(uint16_t));
        memcpy(&question.qclass, qname + qnameLen + 3, sizeof(uint16_t));
        question.qtype = ntohs(question.qtype);
        question.qclass = ntohs(question.qclass);

        // Convert qname to string
        std::string domainName = convertQNameToString(question.qname, question.qnameLength);

        // Find matching record
        const DNSRecord* matchingRecord =
            findMatchingRecord(domainName, static_cast<DNSType>(question.qtype));

        if (matchingRecord) {
            sendResponse(packet, header, question, *matchingRecord);
        } else {
            sendNXDomain(packet, header);
        }
    }

    std::string convertQNameToString(const uint8_t* qname, size_t length) {
        std::string result;
        size_t pos = 0;

        while (pos < length - 1) {
            uint8_t labelLen = qname[pos++];
            if (labelLen == 0) break;

            if (!result.empty()) {
                result += '.';
            }

            result.append(reinterpret_cast<const char*>(qname + pos), labelLen);
            pos += labelLen;
        }

        return result;
    }

    const DNSRecord* findMatchingRecord(const std::string& domain, DNSType qtype) {
        // First try exact match
        for (const auto& record : records) {
            // Match all domains if record.domain is "*"
            if ((record.domain == "*" || record.domain == domain) &&
                (record.type == qtype || qtype == static_cast<DNSType>(255))) {
                return &record;
            }
        }

        // Then try regex wildcard match
        for (const auto& record : records) {
            // Skip exact matches and catch-all
            if (record.domain == "*" || record.domain == domain) {
                continue;
            }

            // Convert DNS wildcard to regex pattern
            if (record.domain.find('*') != std::string::npos) {
                std::string pattern = record.domain;
                // Replace "." with "\." and "*" with ".*"
                size_t pos = 0;
                while ((pos = pattern.find('.', pos)) != std::string::npos) {
                    pattern.insert(pos, "\\");
                    pos += 2;
                }
                pos = 0;
                while ((pos = pattern.find('*', pos)) != std::string::npos) {
                    pattern.replace(pos, 1, ".*");
                    pos += 2;
                }

                try {
                    std::regex re(pattern);
                    if (std::regex_match(domain, re) &&
                        (record.type == qtype || qtype == static_cast<DNSType>(255))) {
                        return &record;
                    }
                } catch (const std::regex_error&) {
                    continue;
                }
            }
        }

        // Finally check wildcardDomains map for custom patterns
        for (const auto& [pattern, domains] : wildcardDomains) {
            try {
                std::regex re(pattern);
                if (std::regex_match(domain, re)) {
                    for (const auto& record : records) {
                        if (record.type == qtype || qtype == static_cast<DNSType>(255)) {
                            return &record;
                        }
                    }
                }
            } catch (const std::regex_error&) {
                continue;
            }
        }

        return nullptr;
    }

    void sendResponse(
        AsyncUDPPacket& request,
        DNSHeader& header,
        const DNSQuestion& question,
        const DNSRecord& record
    ) {
        AsyncUDPMessage response;

        // Prepare header
        header.flags = 0x8180;  // Standard response
        header.ancount = htons(1);
        header.flags = htons(header.flags);

        // Write header
        response.write(reinterpret_cast<uint8_t*>(&header), sizeof(DNSHeader));

        // Write question
        response.write(question.qname, question.qnameLength);
        uint16_t temp = htons(question.qtype);
        response.write(reinterpret_cast<uint8_t*>(&temp), 2);
        temp = htons(question.qclass);
        response.write(reinterpret_cast<uint8_t*>(&temp), 2);

        // Write answer
        response.write((uint8_t)0xC0);
        response.write((uint8_t)0x0C);

        temp = htons(static_cast<uint16_t>(record.type));
        response.write(reinterpret_cast<uint8_t*>(&temp), 2);
        temp = htons(1);  // Class IN
        response.write(reinterpret_cast<uint8_t*>(&temp), 2);

        uint32_t ttl = htonl(record.ttl);
        response.write(reinterpret_cast<uint8_t*>(&ttl), 4);

        if (record.type == DNSType::A) {
            temp = htons(4);  // IPv4 length
            response.write(reinterpret_cast<uint8_t*>(&temp), 2);
            uint32_t ip = record.ip;
            response.write(reinterpret_cast<uint8_t*>(&ip), 4);
        } else {
            // Handle other record types here
            temp = htons(record.data.length());
            response.write(reinterpret_cast<uint8_t*>(&temp), 2);
            response.write(
                reinterpret_cast<const uint8_t*>(record.data.c_str()), record.data.length()
            );
        }

        _udp.sendTo(response, request.remoteIP(), request.remotePort());
    }

    void sendNXDomain(AsyncUDPPacket& request, DNSHeader& header) {
        AsyncUDPMessage response;

        header.flags = 0x8183;  // NXDOMAIN response
        header.flags = htons(header.flags);
        header.ancount = 0;

        response.write(reinterpret_cast<uint8_t*>(&header), sizeof(DNSHeader));
        _udp.sendTo(response, request.remoteIP(), request.remotePort());
    }

    static AsyncDNSServer create(uint16_t port) {
        return AsyncDNSServer(port);
    }

   public:
    // Delete copy constructor and assignment operator
    AsyncDNSServer(const AsyncDNSServer&) = delete;
    AsyncDNSServer& operator=(const AsyncDNSServer&) = delete;

    // Add move constructor and assignment operator
    AsyncDNSServer(AsyncDNSServer&& other) noexcept
        : _udp(std::move(other._udp)),
          port(other.port),
          defaultTTL(other.defaultTTL),
          records(std::move(other.records)),
          wildcardDomains(std::move(other.wildcardDomains)) {}

    AsyncDNSServer& operator=(AsyncDNSServer&& other) noexcept {
        if (this != &other) {
            _udp = std::move(other._udp);
            port = other.port;
            defaultTTL = other.defaultTTL;
            records = std::move(other.records);
            wildcardDomains = std::move(other.wildcardDomains);
        }
        return *this;
    }

    // Get instance for specific port
    static AsyncDNSServer& getInstance(uint16_t port = 53) {
        if (xSemaphoreTake(instanceMutex, portMAX_DELAY) != pdTRUE) {
            static AsyncDNSServer fallback(port);
            return fallback;
        }

        auto it = instances.find(port);
        if (it != instances.end() && it->second.isRunning()) {
            xSemaphoreGive(instanceMutex);
            return it->second;
        }

        auto [inserted_it, success] = instances.insert({port, create(port)});
        xSemaphoreGive(instanceMutex);
        return inserted_it->second;
    }

    bool start() {
        bool success = _udp.listen(port);
        if (!success) {
            return false;
        }

        _udp.onPacket([this](AsyncUDPPacket packet) { this->handleQuery(packet); });
        return true;
    }

    void stop() {
        _udp.close();
    }

    bool isRunning() {
        return _udp.connected() ? true : false;
    }

    void addRecord(
        const std::string& domain, const IPAddress& ip, DNSType type = DNSType::A, uint32_t ttl = 60
    ) {
        records.push_back({domain, type, ip, "", ttl});
    }

    void addRecord(
        const std::string& domain, const std::string& data, DNSType type, uint32_t ttl = 60
    ) {
        records.push_back({domain, type, IPAddress(), data, ttl});
    }

    void addWildcardDomain(const std::string& pattern, const std::vector<std::string>& domains) {
        wildcardDomains[pattern] = domains;
    }

    void clearRecords() {
        records.clear();
    }

    void setDefaultTTL(uint32_t ttl) {
        defaultTTL = ttl;
    }
};

// Initialize static members
std::map<uint16_t, AsyncDNSServer> AsyncDNSServer::instances;
SemaphoreHandle_t AsyncDNSServer::instanceMutex = nullptr;