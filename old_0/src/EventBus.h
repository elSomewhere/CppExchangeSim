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
#include <algorithm>   // Required for std::find_if, std::equal, std::remove
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
    using SequenceNumber = uint64_t;
    // --- String Interning ---
    using InternedStringId = uint64_t;
    const InternedStringId INVALID_ID = 0; // Reserve 0 for invalid/empty ID
    using TopicId = InternedStringId;
    using StreamId = InternedStringId;

    // --- Wildcard Constants ---
    const std::string SINGLE_LEVEL_WILDCARD = "*";
    const std::string MULTI_LEVEL_WILDCARD = "#";


    // --- Forward declare Event Types (Needed by IEventProcessor) ---
    // This assumes the specific event types are known globally or passed down.
    // We will use a template alias later to make this cleaner.
    // template<typename... EventTypes> class TopicBasedEventBus;
    // template<typename... EventTypes> class IEventProcessor; // Forward needed for Bus
    // template<typename Derived, typename... EventTypes> class EventProcessor; // Forward needed? Maybe not.


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
        struct ScheduledEvent {
            Timestamp scheduled_time;
            EventVariant event;
            TopicId topic;
            AgentId publisher_id;
            AgentId subscriber_id;
            Timestamp publish_time;
            StreamId stream_id = INVALID_ID;
            SequenceNumber sequence_number = 0;

            bool operator>(const ScheduledEvent &other) const {
                return scheduled_time > other.scheduled_time;
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
                Timestamp process_time,
                StreamId stream_id,
                SequenceNumber seq_num
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

        // Last processed timestamp for *incoming* streams
        std::unordered_map<std::pair<StreamId, AgentId>, Timestamp, PairHasher> sub_stream_last_ts_;

        // Flag for re-entrancy check (now protected)
        bool is_processing_flag_ = false;

        // Allow Bus access (Needed for ProcessingGuard access to is_processing_flag_)
        // Note: ProcessingGuard now uses the virtual interface methods.
        // Keeping friend declaration might still be useful if direct access is ever needed,
        // but ideally interactions go via the IEventProcessor interface.
        friend class TopicBasedEventBus<EventTypes...>;

        // Helper template struct for static_assert (still useful inside derived handle_event if needed)
        template<typename T>
        struct dependent_false : std::false_type {
        };

        // --- Default handler implementation (kept separate) ---
        // Derived classes must provide their own NON-TEMPLATE handle_event overloads.
        // They can explicitly call this default implementation if desired.
        template<typename E>
        void handle_event_default(const E &event, TopicId published_topic_id, AgentId publisher_id,
                                  Timestamp process_time, StreamId stream_id, SequenceNumber seq_num) {
            LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Agent " + std::to_string(id_) +
                                                                   " handled event type '" + std::string(typeid(E).name()) +
                                                                   "' via DEFAULT handler. PubTopic='" + get_topic_string(
                    published_topic_id) +
                                                                   "', Stream=" + get_stream_string(stream_id));
        }

    public:
        EventProcessor(AgentId id) : id_(id) {}

        // --- Implement IEventProcessor pure virtual methods ---
        AgentId get_id() const override { return id_; }
        void set_event_bus(TopicBasedEventBus<EventTypes...>* bus) override { bus_ = bus; }

        // The public interface for processing, called by the bus
        void process_event_variant(
                const EventVariant& event_variant,
                TopicId published_topic_id,
                AgentId publisher_id,
                Timestamp process_time,
                StreamId stream_id,
                SequenceNumber seq_num
        ) override {
            // Call the internal CRTP-dispatching method
            process_event_internal(event_variant, published_topic_id, publisher_id, process_time, stream_id, seq_num);
        }

        void queue_reentrant_event(ScheduledEvent&& event) override {
            reentrant_event_queue_.push_back(std::move(event));
        }

        void flush_streams() override {
            // Reschedule any events that were queued due to re-entrancy
            if (!reentrant_event_queue_.empty() && bus_) {
                LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Agent " + std::to_string(id_) +
                                                                       " flushing " + std::to_string(
                        reentrant_event_queue_.size()) +
                                                                       " re-entrant events.");
                for (auto &scheduled_event: reentrant_event_queue_) {
                    // Use the bus's reschedule method (assuming it exists or adding it)
                    // TopicBasedEventBus needs a way to reschedule
                    if(bus_) bus_->reschedule_event(std::move(scheduled_event));
                }
                reentrant_event_queue_.clear();
            }
            // Add application-specific logic here if needed by derived classes
            // by overriding this virtual method *if necessary*.
        }

        // Implement accessors for the processing flag
        bool is_processing() const override { return is_processing_flag_; }
        void set_processing(bool is_processing) override { is_processing_flag_ = is_processing; }

        // --- Internal Event Processing Logic using CRTP ---
        // This is called by the public process_event_variant wrapper.
        void process_event_internal(
                const EventVariant& event_variant,
                TopicId published_topic_id,
                AgentId publisher_id,
                Timestamp process_time,
                StreamId stream_id,
                SequenceNumber seq_num
        ) {
            // Stream dependency check (example)
            if (stream_id != INVALID_ID) {
                auto stream_key = std::make_pair(stream_id, publisher_id);
                auto it = sub_stream_last_ts_.find(stream_key);
                if (it != sub_stream_last_ts_.end()) {
                    if (process_time < it->second) {
                        // Strict check example (can be modified or removed)
                        LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Agent " + std::to_string(id_) +
                                                                               " received stream event with non-increasing timestamp for StreamID "
                                                                               + std::to_string(stream_id) + " from " + std::to_string(publisher_id)
                                                                               + ". Current ProcessTime: " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(process_time.time_since_epoch()).count())
                                                                               + "ms < Last Ts: " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(it->second.time_since_epoch()).count()) + "ms.");
                    }
                }
                // Update last timestamp *after* processing (moved here from Bus::step)
                // Ensures timestamp reflects when processing actually *finished*
                // Note: This happens regardless of the check above.
                // Potential race if handle_event is async? Not an issue in single thread.
                // Use operator[] for concise insertion/update
                sub_stream_last_ts_[stream_key] = process_time;
            }

            // CRTP Dispatch using std::visit
            std::visit(
                    [&](const auto& event_ptr) {
                        // Cast 'this' to the actual Derived type provided in the template argument
                        // and call its specific handle_event overload.
                        // The derived class MUST implement the correct public handle_event overload.
                        static_cast<Derived*>(this)->handle_event(
                                *event_ptr, // Dereference the shared_ptr to get the const Event&
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
            if (!this->bus_) {
                LogMessage(LogLevel::ERROR, this->get_logger_source(),
                           "Agent " + std::to_string(this->id_) + " cannot schedule_for_self_at: EventBus not set.");
                return;
            }
            if (!event_ptr) {
                LogMessage(LogLevel::WARNING, this->get_logger_source(),
                           "Agent " + std::to_string(this->id_) + " attempted to schedule_for_self_at a null event_ptr for topic: " + full_topic_str_for_self);
                return;
            }

            // Call the bus's schedule_at method, targeting self as publisher and subscriber
            this->bus_->schedule_at(
                this->id_,                      // publisher_id (self)
                this->id_,                      // subscriber_id (self)
                full_topic_str_for_self,
                event_ptr,
                target_execution_time,
                stream_id_str
            );
            LogMessage(LogLevel::DEBUG, this->get_logger_source(),
                       "Agent " + std::to_string(this->id_) + " scheduled event for self at " +
                       std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(target_execution_time.time_since_epoch()).count()) + "us" +
                       " on topic '" + full_topic_str_for_self + "' stream '" + stream_id_str + "': " + event_ptr->to_string());
        }


        // --- Helper methods using the bus ---
        template<typename E>
        void publish(const std::string &topic_str, const std::shared_ptr<const E> &event_ptr,
                     const std::string &stream_id_str = "") {
            if (bus_) {
                bus_->publish(id_, topic_str, event_ptr, stream_id_str);
            } else {
                LogMessage(LogLevel::ERROR, this->get_logger_source(),
                           "Agent " + std::to_string(id_) + " cannot publish: EventBus not set.");
            }
        }

        void subscribe(const std::string &topic_str) {
            if (bus_) {
                bus_->subscribe(id_, topic_str);
            } else {
                LogMessage(LogLevel::ERROR, this->get_logger_source(),
                           "Agent " + std::to_string(id_) + " cannot subscribe: EventBus not set.");
            }
        }

        void unsubscribe(const std::string &topic_str) {
            if (bus_) {
                bus_->unsubscribe(id_, topic_str);
            } else {
                LogMessage(LogLevel::ERROR, this->get_logger_source(),
                           "Agent " + std::to_string(id_) + " cannot unsubscribe: EventBus not set.");
            }
        }

        // --- Get Interned ID / String Helpers ---
        TopicId get_topic_id(const std::string &topic_str) {
            if (bus_) return bus_->intern_topic(topic_str);
            LogMessage(LogLevel::ERROR, this->get_logger_source(), "Cannot get topic ID: EventBus not set.");
            return INVALID_ID;
        }

        StreamId get_stream_id(const std::string &stream_str) {
            if (bus_) return bus_->intern_stream(stream_str);
            LogMessage(LogLevel::ERROR, this->get_logger_source(), "Cannot get stream ID: EventBus not set.");
            return INVALID_ID;
        }

        const std::string &get_topic_string(TopicId id) const {
            if (bus_) return bus_->get_topic_string(id);
            static const std::string err = "[No Bus]";
            return err;
        }

        const std::string &get_stream_string(StreamId id) const {
            if (bus_) return bus_->get_stream_string(id);
            static const std::string err = "[No Bus]";
            return err;
        }

        // NOTE: The base TEMPLATE handle_event<E> with the static_assert has been REMOVED.
        //       Derived classes MUST provide public non-template handle_event overloads
        //       for each event type they intend to process. The CRTP cast in
        //       process_event_internal will fail to compile if a matching overload
        //       is not found in the Derived class.

    }; // End CRTP EventProcessor


    // --- Event Bus ---
    template<typename... EventTypes>
    class TopicBasedEventBus {
    public:
        // Use types defined in IEventProcessor for consistency
        using EventVariant = typename IEventProcessor<EventTypes...>::EventVariant;
        using ScheduledEvent = typename IEventProcessor<EventTypes...>::ScheduledEvent;
        // Alias for the abstract base processor type
        using ProcessorInterface = IEventProcessor<EventTypes...>;

    private:
        Timestamp current_time_;
        std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, std::greater<ScheduledEvent> > event_queue_;

        // Store pointers to the abstract base interface
        std::unordered_map<AgentId, ProcessorInterface*> entities_;

        // --- String Interning --- (Unchanged)
        StringInterner string_interner_;

        // --- Topic Trie --- (Unchanged)
        TrieNode topic_trie_root_;

        // --- Subscription Tracking --- (Unchanged)
        std::unordered_map<AgentId, std::unordered_set<std::string> > agent_exact_subscriptions_;
        std::unordered_map<AgentId, std::unordered_set<std::string> > agent_wildcard_subscriptions_;

        // --- Centralized Sequence Number Generation ---
        // Maps (Publisher AgentId, StreamId) -> Next SequenceNumber
        std::unordered_map<std::pair<AgentId, StreamId>, SequenceNumber, PairHasher> stream_sequence_counters_;
        // Fallback counter for non-streamed or external publisher events
        SequenceNumber fallback_sequence_counter_{1};


        // --- Randomness (for latency simulation) --- (Unchanged)
        std::default_random_engine random_engine_;
        std::lognormal_distribution<double> latency_distribution_{0.0, 0.5}; // Example params


        // --- Internal Helper: Get or Create Trie Node --- (Unchanged)
        TrieNode *find_or_create_node(const std::string &topic_str, bool create_if_missing = true) {
            // ... (implementation unchanged)
            if (topic_str.empty()) {
                return &topic_trie_root_; // Root node for ""
            }
            if (is_wildcard_topic(topic_str)) {
                LogMessage(LogLevel::ERROR, get_logger_source(),
                           "Internal Error: find_or_create_node called with wildcard topic: " + topic_str);
                return nullptr; // Should not happen if called correctly
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


        // --- Internal Event Scheduling ---
        void schedule_event_for_subscriber(
                const EventVariant &event_variant,
                TopicId published_topic_id,
                AgentId publisher_id,
                AgentId subscriber_id,
                Timestamp original_publish_time,
                StreamId stream_id,
                Duration processing_latency
        ) {
            ProcessorInterface *receiver = nullptr; // Use interface pointer
            auto it = entities_.find(subscriber_id);
            if (it != entities_.end()) {
                receiver = it->second;
            } else {
                // LogMessage(LogLevel::DEBUG, get_logger_source(), "Dropping event: Target subscriber " + std::to_string(subscriber_id) + " not registered.");
                return;
            }

            // --- Centralized Sequence Number Generation ---
            SequenceNumber next_seq_num = 0;
            if (stream_id != INVALID_ID) {
                // Use the bus's central counter map
                auto stream_key = std::make_pair(publisher_id, stream_id);
                // Use operator[]: inserts if not present (default constructs SequenceNumber=0), then pre-increments
                next_seq_num = ++(stream_sequence_counters_[stream_key]);
            } else {
                // Non-streamed event, use fallback counter
                next_seq_num = ++fallback_sequence_counter_;
            }

            ScheduledEvent scheduled_event{
                    .scheduled_time = current_time_ + processing_latency,
                    .event = event_variant, // Copy variant
                    .topic = published_topic_id,
                    .publisher_id = publisher_id,
                    .subscriber_id = subscriber_id,
                    .publish_time = original_publish_time,
                    .stream_id = stream_id,
                    .sequence_number = next_seq_num
            };

            // Check for re-entrancy using the receiver's interface method
            if (receiver && receiver->is_processing()) {
                // Queue event using the receiver's interface method
                receiver->queue_reentrant_event(std::move(scheduled_event));
            } else {
                event_queue_.push(std::move(scheduled_event));
            }
        }

        std::string get_logger_source() const { return "EventBus"; }

    public:
        TopicBasedEventBus(Timestamp start_time = Timestamp{}) : current_time_(start_time) {
            random_engine_.seed(std::chrono::steady_clock::now().time_since_epoch().count());
            if (string_interner_.intern("") != INVALID_ID) {
                throw std::logic_error("String interner failed to map empty string to INVALID_ID");
            }
            topic_trie_root_.topic_id = INVALID_ID;
            topic_trie_root_.parent = nullptr;
            topic_trie_root_.part_key = "";
        }

        // Prevent copying/assignment
        TopicBasedEventBus(const TopicBasedEventBus &) = delete;
        TopicBasedEventBus &operator=(const TopicBasedEventBus &) = delete;

        // Allow moving
        TopicBasedEventBus(TopicBasedEventBus &&) = default;
        TopicBasedEventBus &operator=(TopicBasedEventBus &&) = default;


        // --- Entity Management ---
        // Accepts pointer to the abstract base interface
        void register_entity(AgentId id, ProcessorInterface *entity) {
            if (!entity) return;
            // Ensure ID matches the entity's reported ID
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
            // Set the bus pointer using the interface method
            entity->set_event_bus(this);
            LogMessage(LogLevel::INFO, get_logger_source(), "Registered entity ID: " + std::to_string(id));
        }

        void deregister_entity(AgentId id) {
            auto entity_it = entities_.find(id);
            if (entity_it == entities_.end()) {
                LogMessage(LogLevel::WARNING, get_logger_source(),
                           "Attempted to deregister non-existent entity ID: " + std::to_string(id));
                return;
            }

            ProcessorInterface* entity = entity_it->second;

            // Unsubscribe from all exact topics
            auto exact_sub_it = agent_exact_subscriptions_.find(id);
            if (exact_sub_it != agent_exact_subscriptions_.end()) {
                // Copy strings to avoid iterator invalidation while calling unsubscribe
                std::vector<std::string>
                        topics_to_unsubscribe(exact_sub_it->second.begin(), exact_sub_it->second.end());
                for (const std::string &topic_str: topics_to_unsubscribe) {
                    unsubscribe(id, topic_str); // Will handle Trie removal and pruning, AND cleanup agent_exact_subscriptions_ map entry if needed
                }
                // REMOVE THE LINE BELOW - unsubscribe already handles map cleanup
                // agent_exact_subscriptions_.erase(exact_sub_it); // <-- DELETE THIS LINE
            }

            // Unsubscribe from all wildcard topics
            auto wildcard_sub_it = agent_wildcard_subscriptions_.find(id);
            if (wildcard_sub_it != agent_wildcard_subscriptions_.end()) {
                // Copy strings
                std::vector<std::string> wildcards_to_unsubscribe(wildcard_sub_it->second.begin(),
                                                                  wildcard_sub_it->second.end());
                for (const std::string &wildcard_str: wildcards_to_unsubscribe) {
                    unsubscribe(id, wildcard_str); // Will handle removal from wildcard map AND cleanup agent_wildcard_subscriptions_ map entry if needed
                }
                // REMOVE THE LINE BELOW - unsubscribe already handles map cleanup
                // agent_wildcard_subscriptions_.erase(wildcard_sub_it); // <-- DELETE THIS LINE
            }


            // Clear the bus pointer using the interface method
            entity->set_event_bus(nullptr);
            // Remove from the main entity map
            entities_.erase(entity_it);
            LogMessage(LogLevel::INFO, get_logger_source(), "Deregistered entity ID: " + std::to_string(id));

            // Pending events for this agent will be dropped by step()
        }


        // --- Subscription Management --- (Unchanged logic, uses AgentId)
        void subscribe(AgentId subscriber_id, const std::string &topic_str) {
            if (!entities_.count(subscriber_id)) {
                LogMessage(LogLevel::WARNING, get_logger_source(),
                           "Attempted to subscribe with unregistered ID: " + std::to_string(subscriber_id));
                return;
            }
            // ... rest of subscribe logic unchanged ...
            if (topic_str.find(MULTI_LEVEL_WILDCARD) != std::string::npos &&
                topic_str.find(MULTI_LEVEL_WILDCARD) != topic_str.length() - 1 &&
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
                               topic_str + "' (Node TopicID: " + std::to_string(node->topic_id) + ")");
                } else {
                    LogMessage(LogLevel::DEBUG, get_logger_source(),
                               "Subscriber " + std::to_string(subscriber_id) + " already subscribed to exact topic '" +
                               topic_str + "'");
                }
            }
        }

        void unsubscribe(AgentId subscriber_id, const std::string &topic_str) {
            // ... (implementation unchanged) ...
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


        // --- Event Publishing --- (Largely unchanged logic, uses AgentId)
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
            Timestamp publish_time = current_time_;
            EventVariant event_variant = event_ptr; // Creates variant holding the shared_ptr

            // Find Subscribers (Hierarchical + Wildcard)
            std::unordered_set<AgentId> subscribers_to_notify;
            // ... (subscriber finding logic unchanged) ...
            auto parts = split_topic(topic_str);
            const TrieNode *current_node = &topic_trie_root_;
            std::vector<const TrieNode *> path_nodes;
            path_nodes.push_back(current_node);

            bool exact_path_exists = true;
            for (const auto &part_view: parts) {
                std::string part(part_view);
                auto it = current_node->children.find(part);
                if (it == current_node->children.end()) {
                    exact_path_exists = false;
                    break;
                }
                current_node = it->second.get();
                path_nodes.push_back(current_node);
            }

            if (exact_path_exists || topic_str.empty()) {
                for (auto rit = path_nodes.rbegin(); rit != path_nodes.rend(); ++rit) {
                    const TrieNode *node = *rit;
                    subscribers_to_notify.insert(node->subscribers.begin(), node->subscribers.end());
                }
            }

            for (const auto &[agent_id, wildcard_set]: agent_wildcard_subscriptions_) {
                if (subscribers_to_notify.count(agent_id)) {
                    continue;
                }
                for (const std::string &pattern: wildcard_set) {
                    if (topic_matches_wildcard(pattern, topic_str)) {
                        subscribers_to_notify.insert(agent_id);
                        break;
                    }
                }
            }

            // Schedule for each unique subscriber
            for (AgentId sub_id: subscribers_to_notify) {
                Duration latency = Duration::zero();
#ifndef NDEBUG
                latency = std::chrono::duration_cast<Duration>(
                        std::chrono::duration<double>(latency_distribution_(random_engine_))
                );
                if (latency < Duration::zero()) latency = Duration::zero();
#endif
                // Calls the internal scheduling helper
                schedule_event_for_subscriber(
                        event_variant,
                        published_topic_id,
                        publisher_id,
                        sub_id,
                        publish_time,
                        stream_id,
                        latency
                );
            }
        }

        std::string format_timestamp(Timestamp ts) {
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(ts.time_since_epoch()).count();
            return std::to_string(us) + "us";
        }

        // --- Simulation Step ---
        Timestamp step() {
            if (event_queue_.empty()) {
                return current_time_;
            }

            ScheduledEvent current_event = std::move(event_queue_.top());
            event_queue_.pop();

            if (current_event.scheduled_time >= current_time_) {
                current_time_ = current_event.scheduled_time;
            } else {
                LogMessage(LogLevel::DEBUG, get_logger_source(),
                           "Processing event scheduled at/before current time (Timestamp: "
                           + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(current_event.scheduled_time.time_since_epoch()).count()) + "ms vs Current: "
                           + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(current_time_.time_since_epoch()).count()) + "ms).");
            }

            auto entity_it = entities_.find(current_event.subscriber_id);

            if (entity_it == entities_.end()) {
                LogMessage(LogLevel::INFO, get_logger_source(), // This INFO might be suppressed if G_CURRENT_LOG_LEVEL is higher
                           "Dropping event for deregistered subscriber ID: " + std::to_string(
                                   current_event.subscriber_id));
                return current_time_;
            }

            ProcessorInterface *receiver = entity_it->second; // Get interface pointer

            // --- START: Modified detailed logging for event processing using LogMessage ---
            if (LogLevel::INFO >= LoggerConfig::G_CURRENT_LOG_LEVEL) { // Check if INFO level messages are enabled
                std::ostringstream step_log_oss;
                step_log_oss << "\n>>> [BUS_STEP] Event Popped for Processing <<<\n" // Added markers for visibility
                             << "    Scheduled Time: " << format_timestamp(current_event.scheduled_time) << "\n"
                             << "    Published Time: " << format_timestamp(current_event.publish_time) << "\n"
                             << "    Publisher ID:   " << current_event.publisher_id << "\n"
                             << "    Subscriber ID:  " << current_event.subscriber_id << "\n"
                             << "    Topic:          " << get_topic_string(current_event.topic)
                             << " (ID: " << current_event.topic << ")\n"
                             << "    Stream:         " << get_stream_string(current_event.stream_id)
                             << " (ID: " << current_event.stream_id << ")\n"
                             << "    Sequence Num:   " << current_event.sequence_number << "\n"
                             << "    Event Content:  ";

                std::visit([&](const auto& event_ptr) {
                    if (event_ptr) {
                        step_log_oss << event_ptr->to_string();
                    } else {
                        step_log_oss << "[Null Event Pointer]";
                    }
                }, current_event.event);
                // The LogMessage itself will add a newline, so we don't add one at the end of step_log_oss.str()
                LogMessage(LogLevel::INFO, get_logger_source(), step_log_oss.str());
            }
            // --- END: Modified detailed logging ---


            // --- RAII Guard for Processing Flag (Uses IEventProcessor interface) ---
            class ProcessingGuard {
                ProcessorInterface* proc_; // Store interface pointer
                bool was_processing_;
            public:
                ProcessingGuard(ProcessorInterface* p) : proc_(p), was_processing_(false) {
                    if (proc_) {
                        // Use interface methods to manage the flag
                        was_processing_ = proc_->is_processing();
                        proc_->set_processing(true);
                    }
                }

                ~ProcessingGuard() {
                    if (proc_) {
                        // Restore original flag state via interface
                        proc_->set_processing(was_processing_);
                    }
                }
                ProcessingGuard(const ProcessingGuard &) = delete;
                ProcessingGuard &operator=(const ProcessingGuard &) = delete;
            };


            try {
                ProcessingGuard guard(receiver); // Manages flag via interface

                receiver->process_event_variant(
                        current_event.event,
                        current_event.topic,
                        current_event.publisher_id,
                        current_time_,
                        current_event.stream_id,
                        current_event.sequence_number
                );

            } catch (const std::exception &e) {
                LogMessage(LogLevel::ERROR, get_logger_source(),
                           "Exception during process_event for agent " + std::to_string(current_event.subscriber_id) +
                           ": " + e.what());
            } catch (...) {
                LogMessage(LogLevel::ERROR, get_logger_source(),
                           "Unknown exception during process_event for agent " + std::to_string(
                                   current_event.subscriber_id));
            }

            if (receiver) {
                receiver->flush_streams();
            }

            return current_time_;
        }

        // --- Method to reschedule events (called by EventProcessor's flush_streams) ---
        void reschedule_event(ScheduledEvent &&event) {
            LogMessage(LogLevel::DEBUG, get_logger_source(),
                       "Rescheduling event for agent " + std::to_string(event.subscriber_id) +
                       " originally scheduled for " + std::to_string(
                               std::chrono::duration_cast<std::chrono::milliseconds>(
                                       event.scheduled_time.time_since_epoch()).count()) + "ms");
            event_queue_.push(std::move(event));
        }


        template<typename E>
        void schedule_at(
            AgentId publisher_id,
            AgentId subscriber_id,
            const std::string& topic_str,
            const std::shared_ptr<const E>& event_ptr,
            Timestamp scheduled_execution_time,
            const std::string& stream_id_str = ""
        ) {
            static_assert((std::is_same_v<E, EventTypes> || ...), "Scheduled event type not in EventVariant list");

            if (!event_ptr) {
                LogMessage(LogLevel::WARNING, get_logger_source(),
                           "Attempted to schedule_at a null event pointer for topic: " + topic_str);
                return;
            }

            // Ensure the target subscriber entity is still registered
            auto entity_it = entities_.find(subscriber_id);
            if (entity_it == entities_.end()) {
                LogMessage(LogLevel::INFO, get_logger_source(),
                           "Dropping scheduled_at event: Target subscriber " + std::to_string(subscriber_id) + " not registered.");
                return;
            }
            // ProcessorInterface* receiver = entity_it->second; // Not strictly needed here if directly pushing to queue

            TopicId interned_topic_id = string_interner_.intern(topic_str);
            StreamId interned_stream_id = stream_id_str.empty() ? INVALID_ID : string_interner_.intern(stream_id_str);

            // --- Centralized Sequence Number Generation ---
            SequenceNumber next_seq_num = 0;
            if (interned_stream_id != INVALID_ID) {
                auto stream_key = std::make_pair(publisher_id, interned_stream_id);
                next_seq_num = ++(stream_sequence_counters_[stream_key]);
            } else {
                next_seq_num = ++fallback_sequence_counter_;
            }

            ScheduledEvent scheduled_event{
                .scheduled_time = scheduled_execution_time, // Use the provided execution time
                .event = event_ptr,                         // Convert E to EventVariant
                .topic = interned_topic_id,
                .publisher_id = publisher_id,
                .subscriber_id = subscriber_id,
                .publish_time = current_time_,              // Time of scheduling call
                .stream_id = interned_stream_id,
                .sequence_number = next_seq_num
            };

            // Directly push to the event queue. Re-entrancy at execution time
            // will be handled by the ProcessingGuard in the step() method.
            event_queue_.push(std::move(scheduled_event));

            LogMessage(LogLevel::DEBUG, get_logger_source(),
                       "Event scheduled directly for Agent " + std::to_string(subscriber_id) +
                       " at " + std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(scheduled_execution_time.time_since_epoch()).count()) + "us" +
                       " (Pub: " + std::to_string(publisher_id) + ", Topic: '" + topic_str +
                       "', Stream: '" + stream_id_str + "'): " + event_ptr->to_string());
        }

        Timestamp get_current_time() const {
            return current_time_;
        }

        // --- Utility to get string representation --- (Unchanged)
        const std::string &get_topic_string(TopicId id) const {
            return string_interner_.resolve(id);
        }

        const std::string &get_stream_string(StreamId id) const {
            return string_interner_.resolve(id);
        }

        TopicId intern_topic(const std::string &topic_str) {
            return string_interner_.intern(topic_str);
        }

        StreamId intern_stream(const std::string &stream_str) {
            return string_interner_.intern(stream_str);
        }

        size_t get_event_queue_size() const {
            return event_queue_.size();
        }
    }; // End TopicBasedEventBus

} // namespace EventBusSystem