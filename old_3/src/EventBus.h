//================
// EventBus.h
//================

#pragma once // Use pragma once for header guard

#include <iostream>
#include <vector>
#include <string>
#include <variant>
#include <queue>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <chrono>
#include <typeinfo>
#include <set>
#include <random>
#include <stdexcept>
#include <optional>
#include <memory>      // Required for std::shared_ptr, std::unique_ptr
#include <sstream>     // Required for splitting topic strings, logging
#include <algorithm>   // Required for std::find_if, std::equal, std::remove, std::max
#include <string_view> // Efficient string splitting
#include <utility>     // Required for std::move, std::pair
#include <concepts>    // Required for requires clauses (needs C++20)
#include <iomanip>     // For std::put_time
#include <tuple>       // Used for EventTypeList alias


// Forward Declarations
namespace EventBusSystem {
    using Timestamp = std::chrono::time_point<std::chrono::steady_clock>;
    using Duration = std::chrono::steady_clock::duration;
    using AgentId = uint64_t;
    using SequenceNumber = uint64_t; // Global sequence number
    // --- String Interning ---
    using InternedStringId = uint64_t;
    const InternedStringId INVALID_ID = 0; // Reserve 0 for invalid/empty ID
    using TopicId = InternedStringId;
    using StreamId = InternedStringId;

    // --- Wildcard Constants ---
    const std::string SINGLE_LEVEL_WILDCARD = "*";
    const std::string MULTI_LEVEL_WILDCARD = "#";


    // --- Logging ---
    enum class LogLevel {
        DEBUG, INFO, WARNING, ERROR
    };

    struct LoggerConfig {
        static inline LogLevel G_CURRENT_LOG_LEVEL = LogLevel::INFO;
    };

    inline void LogMessage(LogLevel level, const std::string &source, const std::string &message) {
        if (level >= LoggerConfig::G_CURRENT_LOG_LEVEL) {
            auto now_sys = std::chrono::system_clock::now();
            auto now_c = std::chrono::system_clock::to_time_t(now_sys);
            std::ostringstream oss;
            oss << "[" << std::put_time(std::localtime(&now_c), "%T") << "] "
                << "[" << static_cast<int>(level) << "] "
                << "[" << source << "] "
                << message << std::endl;
            std::cerr << oss.str();
        }
    }

    // --- Custom Hasher for Pairs (Using Boost::hash_combine pattern) ---
    struct PairHasher {
        template<class T1, class T2>
        std::size_t operator()(const std::pair<T1, T2> &p) const {
            static_assert(std::is_invocable_r_v<std::size_t, std::hash<T1>, const T1 &>, "T1 must be hashable");
            static_assert(std::is_invocable_r_v<std::size_t, std::hash<T2>, const T2 &>, "T2 must be hashable");

            std::size_t h1 = std::hash<T1>{}(p.first);
            std::size_t h2 = std::hash<T2>{}(p.second);
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };

    // --- String Interner Class --- (Unchanged)
    class StringInterner {
    private:
        std::unordered_map<std::string, InternedStringId> string_to_id_;
        std::vector<std::string> id_to_string_;
        InternedStringId next_id_ = INVALID_ID + 1;

    public:
        StringInterner() {
            if (id_to_string_.empty()) {
                id_to_string_.emplace_back(""); // id 0 is ""
            }
        }

        InternedStringId intern(const std::string &str) {
            if (str.empty()) return INVALID_ID;
            auto it = string_to_id_.find(str);
            if (it != string_to_id_.end()) {
                return it->second;
            }
            InternedStringId new_id = next_id_++;
            if (next_id_ == INVALID_ID) {
                throw std::overflow_error("StringInterner ID overflow");
            }
            auto [map_it, inserted] = string_to_id_.emplace(str, new_id);
            if (!inserted) {
                LogMessage(LogLevel::ERROR, "StringInterner", "Logic error: Failed to insert already checked string.");
                return map_it->second;
            }
            if (new_id >= id_to_string_.size()) {
                id_to_string_.resize(std::max(static_cast<size_t>(new_id + 1), id_to_string_.size() * 2));
            }
            id_to_string_[new_id] = str;
            return new_id;
        }

        const std::string &resolve(InternedStringId id) const {
            if (id >= id_to_string_.size() || id == INVALID_ID) {
                static const std::string invalid_str = "";
                return invalid_str;
            }
            return id_to_string_[id];
        }

        std::optional<InternedStringId> get_id(const std::string &str) const {
            if (str.empty()) return INVALID_ID;
            auto it = string_to_id_.find(str);
            if (it != string_to_id_.end()) {
                return it->second;
            }
            return std::nullopt;
        }
    };


    // --- Trie Node for Hierarchical Topics --- (Unchanged)
    struct TrieNode {
        std::unordered_map<std::string, std::unique_ptr<TrieNode> > children;
        std::unordered_set<AgentId> subscribers;
        TopicId topic_id = INVALID_ID;
        TrieNode *parent = nullptr;
        std::string part_key = "";

        TrieNode() = default;
        TrieNode(const TrieNode &) = delete;
        TrieNode &operator=(const TrieNode &) = delete;
        TrieNode(TrieNode &&) = default;
        TrieNode &operator=(TrieNode &&) = default;

        bool is_prunable() const {
            return subscribers.empty() && children.empty();
        }
    };

    // --- Helper Functions --- (Unchanged)
    inline std::vector<std::string_view> split_topic(std::string_view str, char delim = '.') {
        std::vector<std::string_view> result;
        if (str.empty()) return result;
        size_t start = 0;
        size_t end = str.find(delim);
        while (end != std::string_view::npos) {
            result.push_back(str.substr(start, end - start));
            start = end + 1;
            end = str.find(delim, start);
        }
        result.push_back(str.substr(start));
        return result;
    }

    inline bool is_wildcard_topic(const std::string &topic_str) {
        return topic_str.find(SINGLE_LEVEL_WILDCARD) != std::string::npos ||
               topic_str.find(MULTI_LEVEL_WILDCARD) != std::string::npos;
    }

    inline bool topic_matches_wildcard(const std::string &pattern, const std::string &topic) {
        auto pattern_parts = split_topic(pattern);
        auto topic_parts = split_topic(topic);
        size_t pattern_idx = 0;
        size_t topic_idx = 0;
        while (pattern_idx < pattern_parts.size() && topic_idx < topic_parts.size()) {
            const auto &p_part = pattern_parts[pattern_idx];
            if (p_part == SINGLE_LEVEL_WILDCARD) {
                pattern_idx++;
                topic_idx++;
            } else if (p_part == MULTI_LEVEL_WILDCARD) {
                // Multi-level wildcard must be the last part of the pattern
                return pattern_idx == pattern_parts.size() - 1;
            } else {
                if (p_part != topic_parts[topic_idx]) {
                    return false;
                }
                pattern_idx++;
                topic_idx++;
            }
        }
        bool pattern_consumed = pattern_idx == pattern_parts.size();
        bool topic_consumed = topic_idx == topic_parts.size();
        if (pattern_consumed && topic_consumed) return true;
        // Check if the last part of the pattern was a multi-level wildcard and it matched the remaining topic parts
        if (!pattern_consumed && pattern_parts[pattern_idx] == MULTI_LEVEL_WILDCARD && pattern_idx == pattern_parts.size() - 1) return true;
        return false;
    }

    // --- Forward declarations needed within the namespace ---
    template<typename... EventTypes> class TopicBasedEventBus;
    template<typename... EventTypes> class IEventProcessor;
    template<typename Derived, typename... EventTypes> class EventProcessor;

    // --- Abstract Base Interface for Event Processors ---
    template<typename... EventTypes>
    class IEventProcessor {
    public:
        // Define EventVariant and ScheduledEvent types locally for convenience
        using EventVariant = std::variant<std::shared_ptr<const EventTypes>...>;

        // MODIFIED: ScheduledEvent definition - ADDED operator> for tie-breaking
        struct ScheduledEvent {
            Timestamp scheduled_time;
            EventVariant event;
            TopicId topic;
            AgentId publisher_id;
            AgentId subscriber_id;
            Timestamp publish_time; // Time of the original publish() or schedule_at() call
            StreamId stream_id = INVALID_ID;
            SequenceNumber sequence_number = 0; // Global sequence number

            // For std::priority_queue<..., std::greater<ScheduledEvent>> (min-heap behavior)
            // We want 'true' if 'this' should come AFTER 'other' (has lower priority)
            bool operator>(const ScheduledEvent &other) const {
                if (scheduled_time != other.scheduled_time) {
                    return scheduled_time > other.scheduled_time;
                }
                // Timestamps are equal, use sequence number as tie-breaker
                // Higher sequence number means it was scheduled later, so it comes after.
                return sequence_number > other.sequence_number;
            }
        };

        virtual ~IEventProcessor() = default;

        // --- Pure virtual interface methods needed by the Bus ---
        virtual AgentId get_id() const = 0;
        virtual void set_event_bus(TopicBasedEventBus<EventTypes...>* bus) = 0;

        // Interface for the bus to trigger event processing
        virtual void process_event_variant(
                const EventVariant& event_variant,
                TopicId published_topic_id,
                AgentId publisher_id,
                Timestamp process_time, // This is the current_time_ from the bus when event is processed
                StreamId stream_id,
                SequenceNumber seq_num // The global sequence number of this event
        ) = 0;

        // Interface for the bus to manage re-entrant events
        virtual void queue_reentrant_event(ScheduledEvent&& event) = 0;

        // Interface for the bus to signal stream flushing opportunity
        virtual void flush_streams() = 0;

        // Interface for the processing flag (needed by ProcessingGuard in Bus)
        virtual bool is_processing() const = 0;
        virtual void set_processing(bool is_processing) = 0;

        // --- Helper to get logger source string (can be implemented here) ---
        virtual std::string get_logger_source() const {
            return "Agent " + std::to_string(get_id()); // Relies on get_id()
        }
    };


    // --- CRTP Base Event Processor ---
    template<typename Derived, typename... EventTypes>
    class EventProcessor : public IEventProcessor<EventTypes...> {
    public:
        // Make types accessible from base interface
        using EventVariant = typename IEventProcessor<EventTypes...>::EventVariant;
        using ScheduledEvent = typename IEventProcessor<EventTypes...>::ScheduledEvent;

    protected:
        TopicBasedEventBus<EventTypes...> *bus_ = nullptr;
        AgentId id_ = 0;

        // Re-entrant event queue
        std::vector<ScheduledEvent> reentrant_event_queue_;

        // Last processed timestamp for *incoming* streams (from a specific publisher)
        // This remains useful for the agent's internal logic/diagnostics,
        // even if the bus now handles the primary stream ordering for scheduling.
        std::unordered_map<std::pair<StreamId, AgentId>, Timestamp, PairHasher> sub_stream_last_processed_ts_from_publisher_;

        // Flag for re-entrancy check (now protected)
        bool is_processing_flag_ = false;

        friend class TopicBasedEventBus<EventTypes...>; // May still be useful

        template<typename T>
        struct dependent_false : std::false_type {
        };

        template<typename E>
        void handle_event_default(const E &event, TopicId published_topic_id, AgentId publisher_id,
                                  Timestamp process_time, StreamId stream_id, SequenceNumber seq_num) {
            LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Agent " + std::to_string(id_) +
                                                                   " handled event type '" + std::string(typeid(E).name()) +
                                                                   "' via DEFAULT handler. PubTopic='" + get_topic_string(
                    published_topic_id) +
                                                                   "', Stream=" + get_stream_string(stream_id) +
                                                                   ", Seq=" + std::to_string(seq_num));
        }

    public:
        EventProcessor(AgentId id) : id_(id) {}

        AgentId get_id() const override { return id_; }
        void set_event_bus(TopicBasedEventBus<EventTypes...>* bus) override { bus_ = bus; }

        void process_event_variant(
                const EventVariant& event_variant,
                TopicId published_topic_id,
                AgentId publisher_id,
                Timestamp process_time,
                StreamId stream_id,
                SequenceNumber seq_num
        ) override {
            process_event_internal(event_variant, published_topic_id, publisher_id, process_time, stream_id, seq_num);
        }

        void queue_reentrant_event(ScheduledEvent&& event) override {
            reentrant_event_queue_.push_back(std::move(event));
        }

        void flush_streams() override {
            if (!reentrant_event_queue_.empty() && bus_) {
                LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Agent " + std::to_string(id_) +
                                                                       " flushing " + std::to_string(
                        reentrant_event_queue_.size()) +
                                                                       " re-entrant events.");
                for (auto &scheduled_event: reentrant_event_queue_) {
                    if(bus_) bus_->reschedule_event(std::move(scheduled_event));
                }
                reentrant_event_queue_.clear();
            }
        }

        bool is_processing() const override { return is_processing_flag_; }
        void set_processing(bool is_processing) override { is_processing_flag_ = is_processing; }

        void process_event_internal(
                const EventVariant& event_variant,
                TopicId published_topic_id,
                AgentId publisher_id,
                Timestamp process_time, // This is current_time_ from the bus
                StreamId stream_id,
                SequenceNumber seq_num
        ) {
            if (stream_id != INVALID_ID) {
                auto stream_key = std::make_pair(stream_id, publisher_id);
                // This map now tracks when this agent *processed* an event from a specific publisher on a stream.
                // The bus's new map `subscriber_stream_last_scheduled_ts_` tracks when the event was *scheduled*.
                sub_stream_last_processed_ts_from_publisher_[stream_key] = process_time;
            }

            std::visit(
                    [&](const auto& event_ptr) {
                        static_cast<Derived*>(this)->handle_event(
                                *event_ptr,
                                published_topic_id,
                                publisher_id,
                                process_time,
                                stream_id,
                                seq_num
                        );
                    },
                    event_variant
            );
        }

        template<typename E>
        void schedule_for_self_at(
                Timestamp target_execution_time,
                const std::shared_ptr<const E>& event_ptr,
                const std::string& full_topic_str_for_self,
                const std::string& stream_id_str = ""
        ) {
            if (!this->bus_) { /* ... error log ... */ return; }
            if (!event_ptr) { /* ... error log ... */ return; }

            this->bus_->schedule_at(
                    this->id_,
                    this->id_,
                    full_topic_str_for_self,
                    event_ptr,
                    target_execution_time,
                    stream_id_str
            );
            // ... log ...
        }

        template<typename E>
        void publish(const std::string &topic_str, const std::shared_ptr<const E> &event_ptr,
                     const std::string &stream_id_str = "") {
            if (bus_) {
                bus_->publish(id_, topic_str, event_ptr, stream_id_str);
            } else { /* ... error log ... */ }
        }

        void subscribe(const std::string &topic_str) {
            if (bus_) {
                bus_->subscribe(id_, topic_str);
            } else { /* ... error log ... */ }
        }

        void unsubscribe(const std::string &topic_str) {
            if (bus_) {
                bus_->unsubscribe(id_, topic_str);
            } else { /* ... error log ... */ }
        }

        TopicId get_topic_id(const std::string &topic_str) { /* ... */ return bus_ ? bus_->intern_topic(topic_str) : INVALID_ID; }
        StreamId get_stream_id(const std::string &stream_str) { /* ... */ return bus_ ? bus_->intern_stream(stream_str) : INVALID_ID; }
        const std::string &get_topic_string(TopicId id) const { /* ... */ static const std::string err = "[No Bus]"; return bus_ ? bus_->get_topic_string(id) : err; }
        const std::string &get_stream_string(StreamId id) const { /* ... */ static const std::string err = "[No Bus]"; return bus_ ? bus_->get_stream_string(id) : err; }

    }; // End CRTP EventProcessor


    // --- Event Bus ---
    template<typename... EventTypes>
    class TopicBasedEventBus {
    public:
        using EventVariant = typename IEventProcessor<EventTypes...>::EventVariant;
        using ScheduledEvent = typename IEventProcessor<EventTypes...>::ScheduledEvent;
        using ProcessorInterface = IEventProcessor<EventTypes...>;

    private:
        Timestamp current_time_;
        // MODIFIED: Priority queue definition uses std::greater for min-heap based on ScheduledEvent::operator>
        std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, std::greater<ScheduledEvent> > event_queue_;

        std::unordered_map<AgentId, ProcessorInterface*> entities_;
        StringInterner string_interner_;
        TrieNode topic_trie_root_;
        std::unordered_map<AgentId, std::unordered_set<std::string> > agent_exact_subscriptions_;
        std::unordered_map<AgentId, std::unordered_set<std::string> > agent_wildcard_subscriptions_;

        // NEW: Global sequence counter for tie-breaking (Python-like)
        SequenceNumber global_schedule_sequence_counter_ = 0;

        // NEW: Stores the last scheduled timestamp for a (stream_id, subscriber_id) pair
        // This is for SUBSCRIBER_SEQUENCED ordering (now the only type for publish path)
        std::unordered_map<std::pair<StreamId, AgentId>, Timestamp, PairHasher> subscriber_stream_last_scheduled_ts_;

        std::default_random_engine random_engine_;
        std::lognormal_distribution<double> latency_distribution_{0.0, 0.5}; // Example params, mu=log(mean), sigma

        // --- Internal Helper: Get or Create Trie Node --- (Unchanged)
        TrieNode *find_or_create_node(const std::string &topic_str, bool create_if_missing = true) {
            // ... (implementation unchanged)
            if (topic_str.empty()) {
                return &topic_trie_root_;
            }
            if (is_wildcard_topic(topic_str)) {
                LogMessage(LogLevel::ERROR, get_logger_source(),
                           "Internal Error: find_or_create_node called with wildcard topic: " + topic_str);
                return nullptr;
            }

            auto parts = split_topic(topic_str);
            if (parts.empty() && !topic_str.empty()) {
                LogMessage(LogLevel::WARNING, get_logger_source(),
                           "Topic string '" + topic_str +
                           "' resulted in empty parts after splitting. Treating as root.");
                return &topic_trie_root_;
            }

            TrieNode *current = &topic_trie_root_;
            std::string current_path;

            for (const auto &part_view: parts) {
                std::string part(part_view);
                if (!current_path.empty()) current_path += ".";
                current_path += part;

                auto it = current->children.find(part);
                if (it == current->children.end()) {
                    if (!create_if_missing) {
                        return nullptr;
                    }
                    auto new_node = std::make_unique<TrieNode>();
                    new_node->parent = current;
                    new_node->part_key = part;
                    auto [inserted_it, success] = current->children.emplace(part, std::move(new_node));
                    if (!success) {
                        LogMessage(LogLevel::ERROR, get_logger_source(),
                                   "Failed to insert new Trie node for part: " + part);
                        return nullptr;
                    }
                    current = inserted_it->second.get();
                    current->topic_id = string_interner_.intern(current_path);
                } else {
                    current = it->second.get();
                }
            }
            if (current != &topic_trie_root_ && current->topic_id == INVALID_ID) {
                current->topic_id = string_interner_.intern(topic_str);
            }
            return current;
        }

        // --- Internal Helper: Find Trie Node --- (Unchanged)
        TrieNode *find_node(const std::string &topic_str) const {
            // ... (implementation unchanged)
            if (topic_str.empty()) {
                return const_cast<TrieNode *>(&topic_trie_root_);
            }
            if (is_wildcard_topic(topic_str)) {
                return nullptr;
            }
            auto parts = split_topic(topic_str);
            if (parts.empty() && !topic_str.empty()) {
                return nullptr;
            }
            const TrieNode *current = &topic_trie_root_;
            for (const auto &part_view: parts) {
                std::string part(part_view);
                auto it = current->children.find(part);
                if (it == current->children.end()) {
                    return nullptr;
                }
                current = it->second.get();
            }
            return const_cast<TrieNode *>(current);
        }

        // --- Internal Helper: Prune empty nodes --- (Unchanged)
        void prune_node_path(TrieNode *start_node) {
            // ... (implementation unchanged)
            if (!start_node || start_node == &topic_trie_root_) {
                return;
            }
            TrieNode *current = start_node;
            while (current && current != &topic_trie_root_ && current->is_prunable()) {
                TrieNode *parent = current->parent;
                if (!parent) {
                    LogMessage(LogLevel::ERROR, get_logger_source(),
                               "Pruning error: Node has no parent but is not root.");
                    break;
                }
                const std::string &key = current->part_key;
                if (key.empty() && current != &topic_trie_root_) {
                    LogMessage(LogLevel::ERROR, get_logger_source(),
                               "Pruning error: Node has empty part_key but is not root.");
                    break;
                }
                size_t removed_count = parent->children.erase(key);
                if (removed_count == 0) {
                    LogMessage(LogLevel::WARNING, get_logger_source(),
                               "Pruning warning: Node to prune not found in parent's children using key: " + key);
                }
                current = parent;
            }
        }

        std::string get_logger_source() const { return "EventBus"; }

    public:
        TopicBasedEventBus(Timestamp start_time = Timestamp{}, unsigned int seed = 0)
                : current_time_(start_time) {
            // Seed the random_engine_
            if (seed == 0) { // If seed is 0, use time-based seed for non-determinism
                unsigned int time_seed = static_cast<unsigned int>(
                        std::chrono::high_resolution_clock::now().time_since_epoch().count()
                );
                random_engine_.seed(time_seed);
                LogMessage(LogLevel::INFO, get_logger_source(), "EventBus RNG seeded with time: " + std::to_string(time_seed));
            } else { // Use provided seed
                random_engine_.seed(seed);
                LogMessage(LogLevel::INFO, get_logger_source(), "EventBus RNG seeded with value: " + std::to_string(seed));
            }

            if (string_interner_.intern("") != INVALID_ID) {
                throw std::logic_error("String interner failed to map empty string to INVALID_ID");
            }
            topic_trie_root_.topic_id = INVALID_ID;
            topic_trie_root_.parent = nullptr;
            topic_trie_root_.part_key = "";

            double mean_latency_us = 1000.0;
            double sigma_param = 0.5;
            latency_distribution_ = std::lognormal_distribution<double>(std::log(mean_latency_us), sigma_param);
        }

        TopicBasedEventBus(const TopicBasedEventBus &) = delete;
        TopicBasedEventBus &operator=(const TopicBasedEventBus &) = delete;
        TopicBasedEventBus(TopicBasedEventBus &&) = default;
        TopicBasedEventBus &operator=(TopicBasedEventBus &&) = default;

        void register_entity(AgentId id, ProcessorInterface *entity) { /* ... (unchanged) ... */
            if (!entity) return;
            if(id != entity->get_id()){
                LogMessage(LogLevel::ERROR, get_logger_source(),
                           "Attempted to register entity with mismatched ID: provided " + std::to_string(id) +
                           ", entity has " + std::to_string(entity->get_id()));
                return;
            }

            auto [it, inserted] = entities_.try_emplace(id, entity);
            if (!inserted) {
                LogMessage(LogLevel::WARNING, get_logger_source(),
                           "Attempted to register entity with existing ID: " + std::to_string(id));
                return;
            }
            entity->set_event_bus(this);
            LogMessage(LogLevel::INFO, get_logger_source(), "Registered entity ID: " + std::to_string(id));
        }
        void deregister_entity(AgentId id) { /* ... (unchanged) ... */
            auto entity_it = entities_.find(id);
            if (entity_it == entities_.end()) {
                LogMessage(LogLevel::WARNING, get_logger_source(),
                           "Attempted to deregister non-existent entity ID: " + std::to_string(id));
                return;
            }

            ProcessorInterface* entity = entity_it->second;

            auto exact_sub_it = agent_exact_subscriptions_.find(id);
            if (exact_sub_it != agent_exact_subscriptions_.end()) {
                std::vector<std::string>
                        topics_to_unsubscribe(exact_sub_it->second.begin(), exact_sub_it->second.end());
                for (const std::string &topic_str: topics_to_unsubscribe) {
                    unsubscribe(id, topic_str);
                }
            }

            auto wildcard_sub_it = agent_wildcard_subscriptions_.find(id);
            if (wildcard_sub_it != agent_wildcard_subscriptions_.end()) {
                std::vector<std::string> wildcards_to_unsubscribe(wildcard_sub_it->second.begin(),
                                                                  wildcard_sub_it->second.end());
                for (const std::string &wildcard_str: wildcards_to_unsubscribe) {
                    unsubscribe(id, wildcard_str);
                }
            }

            entity->set_event_bus(nullptr);
            entities_.erase(entity_it);
            LogMessage(LogLevel::INFO, get_logger_source(), "Deregistered entity ID: " + std::to_string(id));
        }

        void subscribe(AgentId subscriber_id, const std::string &topic_str) { /* ... (unchanged) ... */
            if (!entities_.count(subscriber_id)) {
                LogMessage(LogLevel::WARNING, get_logger_source(),
                           "Attempted to subscribe with unregistered ID: " + std::to_string(subscriber_id));
                return;
            }
            if (topic_str.find(MULTI_LEVEL_WILDCARD) != std::string::npos &&
                topic_str.find(MULTI_LEVEL_WILDCARD) != topic_str.length() - 1 &&
                !split_topic(topic_str).empty() && // ensure not just "#"
                split_topic(topic_str).back() != MULTI_LEVEL_WILDCARD) {
                LogMessage(LogLevel::WARNING, get_logger_source(),
                           "Invalid wildcard pattern: '" + MULTI_LEVEL_WILDCARD +
                           "' can only appear as the last part of the topic: " + topic_str);
                return;
            }
            if (is_wildcard_topic(topic_str)) {
                auto [iter, inserted] = agent_wildcard_subscriptions_[subscriber_id].insert(topic_str);
                if (inserted) {
                    LogMessage(LogLevel::INFO, get_logger_source(),
                               "Subscriber " + std::to_string(subscriber_id) + " subscribed to wildcard topic '" +
                               topic_str + "'");
                } else {
                    LogMessage(LogLevel::DEBUG, get_logger_source(),
                               "Subscriber " + std::to_string(subscriber_id) + " already has wildcard subscription '" +
                               topic_str + "'");
                }
            } else {
                TrieNode *node = find_or_create_node(topic_str, true);
                if (!node) {
                    LogMessage(LogLevel::ERROR, get_logger_source(),
                               "Failed to find/create Trie node for exact topic: " + topic_str);
                    return;
                }
                auto [iter, inserted] = node->subscribers.insert(subscriber_id);
                if (inserted) {
                    agent_exact_subscriptions_[subscriber_id].insert(topic_str);
                    LogMessage(LogLevel::INFO, get_logger_source(),
                               "Subscriber " + std::to_string(subscriber_id) + " subscribed to exact topic '" +
                               topic_str + "' (Node TopicID: " + (node->topic_id == INVALID_ID ? "root_or_invalid" : std::to_string(node->topic_id)) + ")");
                } else {
                    LogMessage(LogLevel::DEBUG, get_logger_source(),
                               "Subscriber " + std::to_string(subscriber_id) + " already subscribed to exact topic '" +
                               topic_str + "'");
                }
            }
        }
        void unsubscribe(AgentId subscriber_id, const std::string &topic_str) { /* ... (unchanged) ... */
            bool removed = false;
            TopicId topic_id_hint = string_interner_.get_id(topic_str).value_or(INVALID_ID);

            if (is_wildcard_topic(topic_str)) {
                auto agent_it = agent_wildcard_subscriptions_.find(subscriber_id);
                if (agent_it != agent_wildcard_subscriptions_.end()) {
                    if (agent_it->second.erase(topic_str) > 0) {
                        removed = true;
                    }
                    if (agent_it->second.empty()) {
                        agent_wildcard_subscriptions_.erase(agent_it);
                    }
                }
            } else {
                TrieNode *node = find_node(topic_str);
                if (node) {
                    if (node->subscribers.erase(subscriber_id) > 0) {
                        removed = true;
                        if (node->is_prunable()) {
                            prune_node_path(node);
                        }
                    }
                }
                auto agent_it = agent_exact_subscriptions_.find(subscriber_id);
                if (agent_it != agent_exact_subscriptions_.end()) {
                    // Removed flag might already be true from Trie removal
                    if (agent_it->second.erase(topic_str) > 0) {
                        removed = true;
                    }
                    if (agent_it->second.empty()) {
                        agent_exact_subscriptions_.erase(agent_it);
                    }
                }
            }

            if (removed) {
                LogMessage(LogLevel::INFO, get_logger_source(),
                           "Subscriber " + std::to_string(subscriber_id) + " unsubscribed from topic '" + topic_str +
                           "' (ID hint: " + std::to_string(topic_id_hint) + ")");
            } else {
                LogMessage(LogLevel::WARNING, get_logger_source(),
                           "Attempted to unsubscribe subscriber " + std::to_string(subscriber_id) +
                           " from non-subscribed or non-existent topic: " + topic_str);
            }
        }


        // --- Event Publishing (MODIFIED for SUBSCRIBER_SEQUENCED and Global Seq Num) ---
        template<typename E>
        void publish(
                AgentId publisher_id,
                const std::string &topic_str,
                const std::shared_ptr<const E> &event_ptr,
                const std::string &stream_id_str = ""
        ) {
            static_assert((std::is_same_v<E, EventTypes> || ...), "Event type not in EventVariant list");

            if (is_wildcard_topic(topic_str)) {
                LogMessage(LogLevel::WARNING, get_logger_source(),
                           "Publishing to a topic string containing wildcards is not allowed: " + topic_str);
                return;
            }
            if (!event_ptr) {
                LogMessage(LogLevel::WARNING, get_logger_source(),
                           "Attempted to publish a null event pointer for topic: " + topic_str);
                return;
            }

            TopicId published_topic_id = string_interner_.intern(topic_str);
            StreamId stream_id = stream_id_str.empty() ? INVALID_ID : string_interner_.intern(stream_id_str);
            Timestamp original_publish_time = current_time_; // Time of this publish() call
            EventVariant event_variant = event_ptr;

            // Find Subscribers (Hierarchical + Wildcard) - Unchanged
            std::unordered_set<AgentId> subscribers_to_notify;
            auto parts = split_topic(topic_str);
            const TrieNode *current_trie_node = &topic_trie_root_; // Renamed to avoid conflict
            std::vector<const TrieNode *> path_nodes;
            path_nodes.push_back(current_trie_node);

            bool exact_path_exists = true;
            if (!topic_str.empty()){ // Only traverse if topic_str is not empty
                for (const auto &part_view: parts) {
                    std::string part(part_view);
                    auto it = current_trie_node->children.find(part);
                    if (it == current_trie_node->children.end()) {
                        exact_path_exists = false;
                        break;
                    }
                    current_trie_node = it->second.get();
                    path_nodes.push_back(current_trie_node);
                }
            } else { // If topic_str is empty, only root subscribers are relevant for exact match
                exact_path_exists = true; // Root itself "exists"
            }


            if (exact_path_exists) { // Includes case where topic_str is "" and root subscribers are added
                // Iterate backwards from the specific node up to the root
                for (auto rit = path_nodes.rbegin(); rit != path_nodes.rend(); ++rit) {
                    const TrieNode *node = *rit;
                    subscribers_to_notify.insert(node->subscribers.begin(), node->subscribers.end());
                }
            }
            // Wildcard subscriptions
            for (const auto &[agent_id, wildcard_set]: agent_wildcard_subscriptions_) {
                if (subscribers_to_notify.count(agent_id)) { // Already added by exact or parent match
                    continue;
                }
                for (const std::string &pattern: wildcard_set) {
                    if (topic_matches_wildcard(pattern, topic_str)) {
                        subscribers_to_notify.insert(agent_id);
                        break; // Found a matching wildcard for this agent
                    }
                }
            }
            // --- End Subscriber Finding ---

            if (subscribers_to_notify.empty()) {
                LogMessage(LogLevel::DEBUG, get_logger_source(), "No subscribers found for topic: " + topic_str);
                // Python version warns if no subscribers. Let's do the same if it's not DEBUG level
                if (LoggerConfig::G_CURRENT_LOG_LEVEL <= LogLevel::INFO) {
                    LogMessage(LogLevel::INFO, get_logger_source(), "Warning: No subscribers for topic " + topic_str);
                }
            }


            for (AgentId sub_id: subscribers_to_notify) {
                ProcessorInterface *receiver = nullptr;
                auto entity_it = entities_.find(sub_id);
                if (entity_it != entities_.end()) {
                    receiver = entity_it->second;
                } else {
                    LogMessage(LogLevel::WARNING, get_logger_source(), "Dropping event: Target subscriber " + std::to_string(sub_id) + " not registered (found during publish).");
                    continue;
                }

                Timestamp base_time_for_subscriber = original_publish_time;

                if (stream_id != INVALID_ID) {
                    auto key = std::make_pair(stream_id, sub_id);
                    auto ts_it = subscriber_stream_last_scheduled_ts_.find(key);
                    Timestamp last_sub_stream_ts = (ts_it != subscriber_stream_last_scheduled_ts_.end()) ? ts_it->second : Timestamp{};

                    base_time_for_subscriber = std::max(original_publish_time, last_sub_stream_ts);
                }

                // Sample latency, ensure it's at least 1 microsecond to guarantee progress if timestamps align.
                // Python: max(1, int(val)). We will use std::max with 1us.
                double raw_latency_val = latency_distribution_(random_engine_);
                // Clamp latency (e.g., max 100ms as in Python example)
                double max_latency_seconds = 0.1; // 100ms
                raw_latency_val = std::min(raw_latency_val, max_latency_seconds * 1e6); // Assuming raw_latency_val is in us initially due to distribution params

                Duration latency = std::chrono::duration_cast<Duration>(
                        // Ensure latency is at least 1 microsecond
                        std::chrono::microseconds(std::max(1LL, static_cast<long long>(raw_latency_val)))
                );
                if (latency < Duration::zero()) latency = std::chrono::microseconds(1); // Should not happen with max(1LL,...)

                Timestamp final_scheduled_time = base_time_for_subscriber + latency;

                // Use global sequence counter for tie‐breaking for each scheduled event
                SequenceNumber next_seq_num = ++global_schedule_sequence_counter_;

                ScheduledEvent scheduled_event{
                        .scheduled_time = final_scheduled_time,
                        .event = event_variant,
                        .topic = published_topic_id,
                        .publisher_id = publisher_id,
                        .subscriber_id = sub_id,
                        .publish_time = original_publish_time,
                        .stream_id = stream_id,
                        .sequence_number = next_seq_num
                };

                if (stream_id != INVALID_ID) {
                    subscriber_stream_last_scheduled_ts_[std::make_pair(stream_id, sub_id)] = final_scheduled_time;
                }

                if (receiver && receiver->is_processing()) {
                    receiver->queue_reentrant_event(std::move(scheduled_event));
                } else {
                    event_queue_.push(std::move(scheduled_event));
                }
            }
        }



        std::optional<ScheduledEvent> step() { // Modified to return optional ScheduledEvent
            if (event_queue_.empty()) {
                return std::nullopt; // Return empty optional if queue is empty
            }

            // Need to copy, then pop, then move from copy because top() returns const&
            ScheduledEvent current_event_copy = event_queue_.top();
            event_queue_.pop();
            ScheduledEvent current_event = std::move(current_event_copy);


            if (current_event.scheduled_time < current_time_) {
                LogMessage(LogLevel::ERROR, get_logger_source(),
                           "CRITICAL ERROR: Popped event with timestamp " + format_timestamp(current_event.scheduled_time) +
                           " which is before current_time " + format_timestamp(current_time_) +
                           ". Event Topic: " + get_topic_string(current_event.topic) +
                           ", Seq: " + std::to_string(current_event.sequence_number));
            }
            current_time_ = current_event.scheduled_time;

            auto entity_it = entities_.find(current_event.subscriber_id);
            if (entity_it == entities_.end()) {
                LogMessage(LogLevel::INFO, get_logger_source(),
                           "Dropping event for deregistered subscriber ID: " + std::to_string(current_event.subscriber_id) +
                           " on topic " + get_topic_string(current_event.topic) +
                           " (Seq: " + std::to_string(current_event.sequence_number) + ")");
                return current_event; // Still return the event that was popped
            }

            ProcessorInterface *receiver = entity_it->second;

            // Logging for step (from your provided code)
            if (LogLevel::INFO >= LoggerConfig::G_CURRENT_LOG_LEVEL) {
                std::ostringstream step_log_oss;
                step_log_oss << "\n>>> [BUS_STEP] Event Popped for Processing <<<\n"
                             << "    Scheduled Time: " << format_timestamp(current_event.scheduled_time) << "\n"
                             << "    Publish Call Time: " << format_timestamp(current_event.publish_time) << "\n"
                             << "    Publisher ID:   " << current_event.publisher_id << "\n"
                             << "    Subscriber ID:  " << current_event.subscriber_id << "\n"
                             << "    Topic:          " << get_topic_string(current_event.topic)
                             << " (ID: " << current_event.topic << ")\n"
                             << "    Stream:         " << get_stream_string(current_event.stream_id)
                             << " (ID: " << current_event.stream_id << ")\n"
                             << "    Sequence Num:   " << current_event.sequence_number << "\n"
                             << "    Event Content:  ";
                std::visit([&](const auto& event_ptr) {
                    step_log_oss << event_ptr->to_string();
                }, current_event.event);
                LogMessage(LogLevel::INFO, get_logger_source(), step_log_oss.str());
            }


            class ProcessingGuard {
                ProcessorInterface* proc_;
                bool was_processing_;
            public:
                ProcessingGuard(ProcessorInterface* p) : proc_(p), was_processing_(false) {
                    if (proc_) {
                        was_processing_ = proc_->is_processing();
                        proc_->set_processing(true);
                    }
                }
                ~ProcessingGuard() {
                    if (proc_) {
                        proc_->set_processing(was_processing_);
                    }
                }
                ProcessingGuard(const ProcessingGuard &) = delete;
                ProcessingGuard &operator=(const ProcessingGuard &) = delete;
            };


            try {
                ProcessingGuard guard(receiver);
                receiver->process_event_variant(
                        current_event.event, current_event.topic, current_event.publisher_id,
                        current_time_, current_event.stream_id, current_event.sequence_number
                );
            } catch (const std::exception &e) {
                LogMessage(LogLevel::ERROR, get_logger_source(), "Exception during event processing for agent " +
                                                                 std::to_string(current_event.subscriber_id) + ": " + e.what());
            } catch (...) {
                LogMessage(LogLevel::ERROR, get_logger_source(), "Unknown exception during event processing for agent " +
                                                                 std::to_string(current_event.subscriber_id));
            }

            if (receiver) {
                receiver->flush_streams();
            }
            return current_event; // Return the processed event
        }

        void reschedule_event(ScheduledEvent &&event) {
            LogMessage(LogLevel::DEBUG, get_logger_source(),
                       "Rescheduling event for agent " + std::to_string(event.subscriber_id) +
                       " originally scheduled for " + format_timestamp(event.scheduled_time) +
                       " (Seq: " + std::to_string(event.sequence_number) + ")");
            event_queue_.push(std::move(event));
        }

        // --- Schedule At (MODIFIED for SUBSCRIBER_SEQUENCED and Global Seq Num) ---
        template<typename E>
        void schedule_at(
                AgentId publisher_id, // Can be self (subscriber_id) or another agent
                AgentId subscriber_id,
                const std::string& topic_str,
                const std::shared_ptr<const E>& event_ptr,
                Timestamp target_execution_time,
                const std::string& stream_id_str = ""
        ) {
            static_assert((std::is_same_v<E, EventTypes> || ...), "Scheduled event type not in EventVariant list");

            if (!event_ptr) { /* ... error log ... */ return; }
            auto entity_it = entities_.find(subscriber_id);
            if (entity_it == entities_.end()) { /* ... error log ... */ return; }

            TopicId interned_topic_id = string_interner_.intern(topic_str);
            StreamId interned_stream_id = stream_id_str.empty() ? INVALID_ID : string_interner_.intern(stream_id_str);
            Timestamp call_time = current_time_; // Time of this schedule_at() call

            Timestamp final_execution_time = target_execution_time;
            const Duration min_increment = std::chrono::microseconds(1);

            // Clamp against current time
            final_execution_time = std::max(final_execution_time, call_time + min_increment);

            if (interned_stream_id != INVALID_ID) {
                auto key = std::make_pair(interned_stream_id, subscriber_id);
                auto ts_it = subscriber_stream_last_scheduled_ts_.find(key);
                Timestamp last_sub_stream_ts = (ts_it != subscriber_stream_last_scheduled_ts_.end()) ? ts_it->second : Timestamp{};

                // Also clamp against last scheduled time on this stream for this subscriber + min_increment
                if (last_sub_stream_ts != Timestamp{}) { // Only if there was a previous event
                    final_execution_time = std::max(final_execution_time, last_sub_stream_ts + min_increment);
                }
            }

            SequenceNumber next_seq_num = ++global_schedule_sequence_counter_;

            ScheduledEvent scheduled_event{
                    .scheduled_time = final_execution_time,
                    .event = event_ptr,
                    .topic = interned_topic_id,
                    .publisher_id = publisher_id,
                    .subscriber_id = subscriber_id,
                    .publish_time = call_time,
                    .stream_id = interned_stream_id,
                    .sequence_number = next_seq_num
            };

            if (interned_stream_id != INVALID_ID) {
                subscriber_stream_last_scheduled_ts_[std::make_pair(interned_stream_id, subscriber_id)] = final_execution_time;
            }

            event_queue_.push(std::move(scheduled_event));
            LogMessage(LogLevel::DEBUG, get_logger_source(),
                       "Event scheduled directly for Agent " + std::to_string(subscriber_id) +
                       " at " + format_timestamp(final_execution_time) +
                       " (Pub: " + std::to_string(publisher_id) + ", Topic: '" + topic_str +
                       "', Stream: '" + stream_id_str + "', Seq: " + std::to_string(next_seq_num) + "): " /*+ event_ptr->to_string()*/);
        }


        Timestamp get_current_time() const { return current_time_; }
        const std::string &get_topic_string(TopicId id) const { return string_interner_.resolve(id); }
        const std::string &get_stream_string(StreamId id) const { return string_interner_.resolve(id); }
        TopicId intern_topic(const std::string &topic_str) { return string_interner_.intern(topic_str); }
        StreamId intern_stream(const std::string &stream_str) { return string_interner_.intern(stream_str); }
        size_t get_event_queue_size() const { return event_queue_.size(); }

        std::string format_timestamp(Timestamp ts) const {
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(ts.time_since_epoch()).count();
            return std::to_string(us) + "us";
        }

    }; // End TopicBasedEventBus

} // namespace EventBusSystem