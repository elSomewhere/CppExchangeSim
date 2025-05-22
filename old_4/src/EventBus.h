// file: src/EventBus.h
//================
// EventBus.h
//================

#pragma once // Use pragma once for header guard
#include "Logging.h"
#include <iostream>
#include <vector>
#include <string>
#include <variant>
#include <queue>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <functional> // Required for std::hash, std::function
#include <chrono>
#include <typeinfo>
#include <set>
#include <random>
#include <stdexcept>
#include <optional>
#include <memory>      // Required for std::shared_ptr, std::unique_ptr
#include <sstream>     // Required for splitting topic strings, logging
#include <algorithm>   // Required for std::find_if, std::equal, std::remove, std::max, std::find
#include <string_view> // Efficient string splitting
#include <utility>     // Required for std::move, std::pair

// Forward Declarations
namespace EventBusSystem {
    using Timestamp = std::chrono::time_point<std::chrono::steady_clock>;
    using Duration = std::chrono::steady_clock::duration;
    using AgentId = uint64_t;
    using SequenceNumber = uint64_t; // Global sequence number

    // --- Time unit for latency configuration ---
    using LatencyUnit = std::chrono::microseconds;


    // --- String Interning ---
    using InternedStringId = uint64_t;
    const InternedStringId INVALID_ID_UINT64 = 0; // Reserve 0 for invalid/empty ID
    const AgentId INVALID_AGENT_ID = 0; // Using 0 as an invalid or system/environment ID
    using TopicId = InternedStringId;
    using StreamId = InternedStringId;

    // --- Wildcard Constants ---
    const std::string SINGLE_LEVEL_WILDCARD = "*";
    const std::string MULTI_LEVEL_WILDCARD = "#";


    // --- Latency Parameters Configuration ---
    struct LatencyParameters {
        enum class Type { LOGNORMAL, FIXED } dist_type = Type::LOGNORMAL;

        // For LOGNORMAL
        double lognormal_median_us = 1.0; // Median in microseconds
        double lognormal_sigma = 0.5;     // Sigma for lognormal distribution

        // For FIXED
        double fixed_latency_us = 1.0;    // Fixed latency in microseconds

        // Common
        double max_cap_us = 100000.0;     // Maximum latency cap in microseconds

        LatencyParameters() = default;

        static LatencyParameters Lognormal(double median_us, double sigma, double cap_us) {
            LatencyParameters p;
            p.dist_type = Type::LOGNORMAL;
            p.lognormal_median_us = median_us > 0 ? median_us : 1.0;
            p.lognormal_sigma = sigma > 0 ? sigma : 0.01;
            p.max_cap_us = cap_us >= 0 ? cap_us : 0.0;
            return p;
        }

        static LatencyParameters Fixed(double fixed_us, double cap_us) {
            LatencyParameters p;
            p.dist_type = Type::FIXED;
            p.fixed_latency_us = fixed_us >= 0 ? fixed_us : 0.0;
            p.max_cap_us = cap_us >= 0 ? cap_us : 0.0;
            if (p.max_cap_us > 0 && p.fixed_latency_us > p.max_cap_us) {
                p.fixed_latency_us = p.max_cap_us;
            }
            return p;
        }

        double get_lognormal_mu() const {
            if (lognormal_median_us <= 0) return std::log(1.0);
            return std::log(lognormal_median_us);
        }
    };


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

    // --- String Interner Class ---
    class StringInterner {
    private:
        std::unordered_map<std::string, InternedStringId> string_to_id_;
        std::vector<std::string> id_to_string_;
        InternedStringId next_id_ = INVALID_ID_UINT64 + 1;

    public:
        StringInterner() {
            if (id_to_string_.empty()) {
                id_to_string_.emplace_back("");
            }
        }

        InternedStringId intern(const std::string &str) {
            if (str.empty()) return INVALID_ID_UINT64;
            auto it = string_to_id_.find(str);
            if (it != string_to_id_.end()) {
                return it->second;
            }
            InternedStringId new_id = next_id_++;
            if (next_id_ == INVALID_ID_UINT64) {
                LogMessage(LogLevel::ERROR, "StringInterner", "Critical: StringInterner ID overflow or wrapped to 0.");
                throw std::overflow_error("StringInterner ID overflow or wrapped to invalid ID");
            }
            auto [map_it, inserted] = string_to_id_.emplace(str, new_id);
            if (!inserted) {
                LogMessage(LogLevel::ERROR, "StringInterner", "Logic error: Failed to insert already checked string into string_to_id_ map.");
                return map_it->second;
            }
            if (new_id >= id_to_string_.size()) {
                size_t new_size = std::max(static_cast<size_t>(new_id + 1), id_to_string_.size() + id_to_string_.size() / 2 + 1);
                if (new_size <= new_id) new_size = new_id + 1;
                id_to_string_.resize(new_size);
            }
            id_to_string_[new_id] = str;
            return new_id;
        }

        const std::string &resolve(InternedStringId id) const {
            if (id >= id_to_string_.size()) {
                static const std::string unresolvable_str = "[Unresolvable ID]";
                LogMessage(LogLevel::ERROR, "StringInterner", "Attempted to resolve out-of-bounds ID: " + std::to_string(id));
                return unresolvable_str;
            }
            return id_to_string_[id];
        }

        std::optional<InternedStringId> get_id(const std::string &str) const {
            if (str.empty()) return INVALID_ID_UINT64;
            auto it = string_to_id_.find(str);
            if (it != string_to_id_.end()) {
                return it->second;
            }
            return std::nullopt;
        }
    };


    // --- Trie Node for Hierarchical Topics ---
    struct TrieNode {
        std::unordered_map<std::string, std::unique_ptr<TrieNode> > children;
        std::unordered_set<AgentId> subscribers;
        TopicId topic_id = INVALID_ID_UINT64;
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

    // --- Helper Functions ---
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

        if (topic_consumed && !pattern_consumed) {
            if (pattern_parts[pattern_idx] == MULTI_LEVEL_WILDCARD && pattern_idx == pattern_parts.size() - 1) {
                return true;
            }
        }
        return false;
    }

    // --- Forward declarations needed within the namespace ---
    template<typename... EventTypes> class TopicBasedEventBus; // Forward declare Bus for IPrePublishHook
    template<typename... EventTypes> class IEventProcessor;
    template<typename Derived, typename... EventTypes> class EventProcessor;
    template<typename... EventTypes> class IPrePublishHook; // Forward declare Hook for Bus

    // --- Abstract Base Interface for Event Processors ---
    template<typename... EventTypes>
    class IEventProcessor {
    public:
        using EventVariant = std::variant<std::shared_ptr<const EventTypes>...>;

        struct ScheduledEvent {
            Timestamp scheduled_time;
            EventVariant event;
            TopicId topic;
            AgentId publisher_id;
            AgentId subscriber_id;
            Timestamp publish_time;
            StreamId stream_id = INVALID_ID_UINT64;
            SequenceNumber sequence_number = 0;

            bool operator>(const ScheduledEvent &other) const {
                if (scheduled_time != other.scheduled_time) {
                    return scheduled_time > other.scheduled_time;
                }
                return sequence_number > other.sequence_number;
            }
        };

        virtual ~IEventProcessor() = default;
        virtual AgentId get_id() const = 0;
        virtual void set_id(AgentId id) = 0;
        virtual void set_event_bus(TopicBasedEventBus<EventTypes...>* bus) = 0;
        virtual void process_event_variant(
                const EventVariant& event_variant,
                TopicId published_topic_id,
                AgentId publisher_id,
                Timestamp process_time,
                StreamId stream_id,
                SequenceNumber seq_num
        ) = 0;
        virtual void queue_reentrant_event(ScheduledEvent&& event) = 0;
        virtual void flush_streams() = 0;
        virtual bool is_processing() const = 0;
        virtual void set_processing(bool is_processing) = 0;
        virtual std::string get_logger_source() const {
            return "Agent " + std::to_string(get_id());
        }
    };

    // --- Pre-Publish Hook Interface ---
    template<typename... EventTypes>
    class IPrePublishHook {
    public:
        // Reuse EventVariant from IEventProcessor for consistency
        using EventVariant = typename IEventProcessor<EventTypes...>::EventVariant;

        virtual ~IPrePublishHook() = default;

        /**
         * @brief Called synchronously within TopicBasedEventBus::publish, before the event
         *        is fanned out to subscribers or scheduled.
         * @param publisher_id The ID of the agent that called publish().
         * @param published_topic_id The interned ID of the topic string.
         * @param event_variant The event being published (as a variant of shared_ptr<const E>).
         * @param publish_time The bus's current_time_ at the moment of the publish() call.
         * @param bus A const pointer to the event bus instance, for context (e.g., resolving IDs).
         */
        virtual void on_pre_publish(
                AgentId publisher_id,
                TopicId published_topic_id,
                const EventVariant& event_variant,
                Timestamp publish_time,
                const TopicBasedEventBus<EventTypes...>* bus // Bus pointer for context
        ) = 0;

        virtual std::string get_hook_name() const {
            // Base implementation, can be overridden by concrete hooks for better logging.
            return "UnnamedPrePublishHook";
        }
    };


    // --- CRTP Base Event Processor ---
    template<typename Derived, typename... EventTypes>
    class EventProcessor : public IEventProcessor<EventTypes...> {
    public:
        using EventVariant = typename IEventProcessor<EventTypes...>::EventVariant;
        using ScheduledEvent = typename IEventProcessor<EventTypes...>::ScheduledEvent;

    protected:
        TopicBasedEventBus<EventTypes...> *bus_ = nullptr;
        AgentId id_ = INVALID_AGENT_ID;
        std::vector<ScheduledEvent> reentrant_event_queue_;
        std::unordered_map<std::pair<StreamId, AgentId>, Timestamp, PairHasher> sub_stream_last_processed_ts_from_publisher_;
        bool is_processing_flag_ = false;

        template<typename E>
        void handle_event_default(const E &event, TopicId published_topic_id, AgentId publisher_id,
                                  Timestamp process_time, StreamId stream_id, SequenceNumber seq_num) {
            LogMessage(LogLevel::WARNING, this->get_logger_source(), "Agent " + std::to_string(id_) +
                                                                     " received event type '" + std::string(typeid(E).name()) +
                                                                     "' but has NO specific handler. Using DEFAULT (noop) handler. PubTopic='" + get_topic_string(published_topic_id) +
                                                                     "', Stream=" + get_stream_string(stream_id) +
                                                                     ", Seq=" + std::to_string(seq_num));
        }

    public:
        EventProcessor() : id_(INVALID_AGENT_ID) {}
        AgentId get_id() const override { return id_; }
        void set_id(AgentId new_id) override { id_ = new_id; }
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
            if (!reentrant_event_queue_.empty()) {
                if (bus_) {
                    LogMessage(LogLevel::DEBUG, this->get_logger_source(), "Agent " + std::to_string(id_) +
                                                                           " flushing " + std::to_string(reentrant_event_queue_.size()) +
                                                                           " re-entrant events to bus.");
                    for (auto &scheduled_event: reentrant_event_queue_) {
                        bus_->reschedule_event(std::move(scheduled_event));
                    }
                } else {
                    LogMessage(LogLevel::WARNING, this->get_logger_source(), "Agent " + std::to_string(id_) +
                                                                             " cannot flush re-entrant events: bus_ is null. " +
                                                                             std::to_string(reentrant_event_queue_.size()) + " events dropped.");
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
                Timestamp process_time,
                StreamId stream_id,
                SequenceNumber seq_num
        ) {
            if (stream_id != INVALID_ID_UINT64) {
                auto stream_key = std::make_pair(stream_id, publisher_id);
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
            if (!bus_) {
                LogMessage(LogLevel::ERROR, this->get_logger_source(), "Cannot schedule_for_self_at: EventBus is not set.");
                return;
            }
            if (!event_ptr) {
                LogMessage(LogLevel::ERROR, this->get_logger_source(), "Cannot schedule_for_self_at: event_ptr is null for topic '" + full_topic_str_for_self + "'.");
                return;
            }
            bus_->schedule_at(this->id_, this->id_, full_topic_str_for_self, event_ptr, target_execution_time, stream_id_str);
        }

        template<typename E>
        void publish(const std::string &topic_str, const std::shared_ptr<const E> &event_ptr,
                     const std::string &stream_id_str = "") {
            if (!bus_) {
                LogMessage(LogLevel::ERROR, this->get_logger_source(), "Cannot publish: EventBus is not set.");
                return;
            }
            if (!event_ptr) {
                LogMessage(LogLevel::ERROR, this->get_logger_source(), "Cannot publish: event_ptr is null for topic '" + topic_str + "'.");
                return;
            }
            bus_->publish(id_, topic_str, event_ptr, stream_id_str);
        }

        void subscribe(const std::string &topic_str) {
            if (!bus_) { LogMessage(LogLevel::ERROR, this->get_logger_source(), "Cannot subscribe: EventBus is not set."); return; }
            bus_->subscribe(id_, topic_str);
        }

        void unsubscribe(const std::string &topic_str) {
            if (!bus_) { LogMessage(LogLevel::ERROR, this->get_logger_source(), "Cannot unsubscribe: EventBus is not set."); return; }
            bus_->unsubscribe(id_, topic_str);
        }

        TopicId get_topic_id(const std::string &topic_str) const { return bus_ ? bus_->intern_topic(topic_str) : INVALID_ID_UINT64; }
        StreamId get_stream_id(const std::string &stream_str) const { return bus_ ? bus_->intern_stream(stream_str) : INVALID_ID_UINT64; }
        const std::string &get_topic_string(TopicId id) const { static const std::string err_no_bus = "[No Bus - Topic]"; return bus_ ? bus_->get_topic_string(id) : err_no_bus; }
        const std::string &get_stream_string(StreamId id) const { static const std::string err_no_bus = "[No Bus - Stream]"; return bus_ ? bus_->get_stream_string(id) : err_no_bus; }
    };


    // --- Event Bus ---
    template<typename... EventTypes>
    class TopicBasedEventBus {
    public:
        using EventVariant = typename IEventProcessor<EventTypes...>::EventVariant;
        using ScheduledEvent = typename IEventProcessor<EventTypes...>::ScheduledEvent;
        using ProcessorInterface = IEventProcessor<EventTypes...>;
        using PrePublishHookInterface = IPrePublishHook<EventTypes...>; // Alias for convenience

    private:
        Timestamp current_time_;
        std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, std::greater<ScheduledEvent> > event_queue_;

        std::unordered_map<AgentId, ProcessorInterface*> entities_;
        AgentId next_available_agent_id_ = INVALID_AGENT_ID + 1;

        StringInterner string_interner_;
        TrieNode topic_trie_root_;
        std::unordered_map<AgentId, std::unordered_set<std::string> > agent_exact_subscriptions_;
        std::unordered_map<AgentId, std::unordered_set<std::string> > agent_wildcard_subscriptions_;

        SequenceNumber global_schedule_sequence_counter_ = 0;
        std::unordered_map<std::pair<StreamId, AgentId>, Timestamp, PairHasher> subscriber_stream_last_scheduled_ts_;

        std::default_random_engine random_engine_;
        std::unordered_map<std::pair<AgentId, AgentId>, LatencyParameters, PairHasher> inter_agent_latency_config_;
        LatencyParameters default_latency_params_;

        // --- MODIFICATION: Storage for Pre-Publish Hooks ---
        std::vector<PrePublishHookInterface*> pre_publish_hooks_;
        // --- END MODIFICATION ---


        TrieNode *find_or_create_node(const std::string &topic_str, bool create_if_missing = true) {
            if (topic_str.empty()) { return &topic_trie_root_; }
            if (is_wildcard_topic(topic_str)) {
                LogMessage(LogLevel::ERROR, get_logger_source(), "Internal Error: find_or_create_node called with wildcard topic: " + topic_str);
                return nullptr;
            }
            auto parts = split_topic(topic_str);
            if (parts.empty() && !topic_str.empty()) {
                LogMessage(LogLevel::WARNING, get_logger_source(), "Topic string '" + topic_str + "' resulted in empty parts. Treating as root.");
                return &topic_trie_root_;
            }
            if (parts.empty() && topic_str.empty()) { return &topic_trie_root_; }

            TrieNode *current = &topic_trie_root_;
            std::string current_path_str;
            for (size_t i = 0; i < parts.size(); ++i) {
                const auto &part_view = parts[i];
                std::string part(part_view);
                if (part.empty()) {
                    LogMessage(LogLevel::WARNING, get_logger_source(), "Empty topic segment in: " + topic_str);
                }
                if (!current_path_str.empty()) current_path_str += ".";
                current_path_str += part;

                auto it = current->children.find(part);
                if (it == current->children.end()) {
                    if (!create_if_missing) return nullptr;
                    auto new_node = std::make_unique<TrieNode>();
                    new_node->parent = current;
                    new_node->part_key = part;
                    auto [inserted_it, success] = current->children.emplace(part, std::move(new_node));
                    if (!success) {
                        LogMessage(LogLevel::ERROR, get_logger_source(), "Failed to insert Trie node for part: '" + part + "' in '" + topic_str + "'.");
                        return nullptr;
                    }
                    current = inserted_it->second.get();
                    current->topic_id = string_interner_.intern(current_path_str);
                } else {
                    current = it->second.get();
                }
            }
            if (current != &topic_trie_root_ && current->topic_id == INVALID_ID_UINT64) {
                current->topic_id = string_interner_.intern(topic_str);
            } else if (current == &topic_trie_root_ && topic_str.empty()){
                current->topic_id = INVALID_ID_UINT64;
            }
            return current;
        }

        TrieNode *find_node(const std::string &topic_str) const {
            if (topic_str.empty()) return const_cast<TrieNode *>(&topic_trie_root_);
            if (is_wildcard_topic(topic_str)) {
                LogMessage(LogLevel::DEBUG, get_logger_source(), "find_node called with wildcard topic: " + topic_str);
                return nullptr;
            }
            auto parts = split_topic(topic_str);
            if (parts.empty() && !topic_str.empty()) {
                LogMessage(LogLevel::DEBUG, get_logger_source(), "find_node: Topic '" + topic_str + "' resulted in empty parts.");
                return nullptr;
            }
            if (parts.empty() && topic_str.empty()) return const_cast<TrieNode *>(&topic_trie_root_);

            const TrieNode *current = &topic_trie_root_;
            for (const auto &part_view: parts) {
                std::string part(part_view);
                if (part.empty()){
                    LogMessage(LogLevel::DEBUG, get_logger_source(), "find_node: Empty part in topic " + topic_str);
                }
                auto it = current->children.find(part);
                if (it == current->children.end()) return nullptr;
                current = it->second.get();
            }
            return const_cast<TrieNode *>(current);
        }

        void prune_node_path(TrieNode *start_node) {
            if (!start_node || start_node == &topic_trie_root_) return;
            TrieNode *current = start_node;
            while (current && current != &topic_trie_root_ && current->is_prunable()) {
                TrieNode *parent = current->parent;
                if (!parent) {
                    LogMessage(LogLevel::ERROR, get_logger_source(), "Pruning error: Node (part_key: '" + current->part_key + "') has no parent.");
                    break;
                }
                const std::string &key_to_remove = current->part_key;
                if (key_to_remove.empty() && current != &topic_trie_root_) {
                    LogMessage(LogLevel::WARNING, get_logger_source(), "Pruning node with empty part_key.");
                }
                size_t removed_count = parent->children.erase(key_to_remove);
                if (removed_count == 0) {
                    LogMessage(LogLevel::WARNING, get_logger_source(), "Pruning anomaly: Node (topic_id: " + std::to_string(current->topic_id) +
                                                                       ") with part_key '" + key_to_remove + "' not found in parent's (topic_id: " + std::to_string(parent->topic_id) + ") children.");
                } else {
                    LogMessage(LogLevel::DEBUG, get_logger_source(), "Pruned TrieNode part_key: '" + key_to_remove + "'.");
                }
                current = parent;
            }
        }

        std::string get_logger_source() const { return "EventBus"; }

    public:
        TopicBasedEventBus(Timestamp start_time = Timestamp{},
                           unsigned int seed = 0,
                           double global_median_latency_us = 1.0,
                           double global_sigma_for_lognormal = 0.5,
                           double global_max_latency_cap_us = 100000.0)
                : current_time_(start_time) {
            unsigned int actual_seed = seed;
            if (seed == 0) {
                actual_seed = static_cast<unsigned int>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
                LogMessage(LogLevel::INFO, get_logger_source(), "EventBus RNG seeded with time: " + std::to_string(actual_seed));
            } else {
                LogMessage(LogLevel::INFO, get_logger_source(), "EventBus RNG seeded with value: " + std::to_string(actual_seed));
            }
            random_engine_.seed(actual_seed);

            default_latency_params_ = LatencyParameters::Lognormal(global_median_latency_us, global_sigma_for_lognormal, global_max_latency_cap_us);
            LogMessage(LogLevel::INFO, get_logger_source(), "Default latency: Lognormal (Median: " + std::to_string(default_latency_params_.lognormal_median_us) +
                                                            "us, Sigma: " + std::to_string(default_latency_params_.lognormal_sigma) + ", Cap: " + std::to_string(default_latency_params_.max_cap_us) + "us)");

            if (string_interner_.intern("") != INVALID_ID_UINT64) {
                LogMessage(LogLevel::ERROR, get_logger_source(), "String interner failed for empty string on init.");
                throw std::logic_error("String interner init error");
            }
            topic_trie_root_.topic_id = INVALID_ID_UINT64;
            topic_trie_root_.parent = nullptr;
            topic_trie_root_.part_key = "";
        }

        TopicBasedEventBus(const TopicBasedEventBus &) = delete;
        TopicBasedEventBus &operator=(const TopicBasedEventBus &) = delete;
        TopicBasedEventBus(TopicBasedEventBus &&) = default;
        TopicBasedEventBus &operator=(TopicBasedEventBus &&) = default;

        void set_inter_agent_latency(AgentId publisher_id, AgentId subscriber_id, const LatencyParameters& params) {
            inter_agent_latency_config_[std::make_pair(publisher_id, subscriber_id)] = params;
            std::string type_str = params.dist_type == LatencyParameters::Type::LOGNORMAL ? "Lognormal" : "Fixed";
            double primary_val = params.dist_type == LatencyParameters::Type::LOGNORMAL ? params.lognormal_median_us : params.fixed_latency_us;
            LogMessage(LogLevel::INFO, get_logger_source(), "Set latency " + std::to_string(publisher_id) + "->" + std::to_string(subscriber_id) +
                                                            " (Type:" + type_str + ",Val:" + std::to_string(primary_val) + "us,Cap:" + std::to_string(params.max_cap_us) + "us)");
        }

        void clear_inter_agent_latency(AgentId publisher_id, AgentId subscriber_id) {
            if (inter_agent_latency_config_.erase(std::make_pair(publisher_id, subscriber_id)) > 0) {
                LogMessage(LogLevel::INFO, get_logger_source(), "Cleared latency " + std::to_string(publisher_id) + "->" + std::to_string(subscriber_id));
            }
        }

        void set_default_latency(const LatencyParameters& params) {
            default_latency_params_ = params;
            std::string type_str = params.dist_type == LatencyParameters::Type::LOGNORMAL ? "Lognormal" : "Fixed";
            double primary_val = params.dist_type == LatencyParameters::Type::LOGNORMAL ? params.lognormal_median_us : params.fixed_latency_us;
            LogMessage(LogLevel::INFO, get_logger_source(), "Set default latency (Type:" + type_str +
                                                            ",Val:" + std::to_string(primary_val) + "us,Cap:" + std::to_string(params.max_cap_us) + "us)");
        }

        // --- MODIFICATION: Pre-Publish Hook Management ---
        void register_pre_publish_hook(PrePublishHookInterface* hook) {
            if (!hook) {
                LogMessage(LogLevel::WARNING, get_logger_source(), "Attempted to register a null pre-publish hook.");
                return;
            }
            // Avoid duplicate registrations
            if (std::find(pre_publish_hooks_.begin(), pre_publish_hooks_.end(), hook) != pre_publish_hooks_.end()) {
                LogMessage(LogLevel::DEBUG, get_logger_source(), "Pre-publish hook '" + hook->get_hook_name() + "' is already registered. Ignoring.");
                return;
            }
            pre_publish_hooks_.push_back(hook);
            LogMessage(LogLevel::INFO, get_logger_source(), "Registered pre-publish hook: " + hook->get_hook_name());
        }

        void deregister_pre_publish_hook(PrePublishHookInterface* hook) {
            if (!hook) {
                LogMessage(LogLevel::WARNING, get_logger_source(), "Attempted to deregister a null pre-publish hook.");
                return;
            }
            auto it = std::remove(pre_publish_hooks_.begin(), pre_publish_hooks_.end(), hook);
            if (it != pre_publish_hooks_.end()) {
                pre_publish_hooks_.erase(it, pre_publish_hooks_.end());
                LogMessage(LogLevel::INFO, get_logger_source(), "Deregistered pre-publish hook: " + hook->get_hook_name());
            } else {
                LogMessage(LogLevel::WARNING, get_logger_source(), "Attempted to deregister a non-registered pre-publish hook: " + hook->get_hook_name());
            }
        }
        // --- END MODIFICATION ---


        void register_entity_with_id(AgentId id, ProcessorInterface *entity) {
            if (!entity) {
                LogMessage(LogLevel::ERROR, get_logger_source(), "Register null entity with ID: " + std::to_string(id)); return;
            }
            if (id < next_available_agent_id_ && entities_.count(id) && id != INVALID_AGENT_ID) {
                LogMessage(LogLevel::WARNING, get_logger_source(), "Registering ID " + std::to_string(id) + " which is in use or < next auto-ID.");
            }
            auto [it, inserted] = entities_.try_emplace(id, entity);
            if (!inserted) {
                LogMessage(LogLevel::WARNING, get_logger_source(), "Entity ID " + std::to_string(id) + " already registered. Failed.");
                if (it->second != entity) LogMessage(LogLevel::ERROR, get_logger_source(), "CRITICAL: ID " + std::to_string(id) + " registered to DIFFERENT entity!");
                return;
            }
            entity->set_id(id);
            entity->set_event_bus(this);
            LogMessage(LogLevel::INFO, get_logger_source(), "Registered entity with ID: " + std::to_string(id) + " (Type: " + typeid(*entity).name() + ")");
            if (id >= next_available_agent_id_ && id != INVALID_AGENT_ID) next_available_agent_id_ = id + 1;
        }

        AgentId register_entity(ProcessorInterface* entity) {
            if (!entity) {
                LogMessage(LogLevel::ERROR, get_logger_source(), "Register null entity."); return INVALID_AGENT_ID;
            }
            AgentId assigned_id = next_available_agent_id_;
            while (entities_.count(assigned_id) || assigned_id == INVALID_AGENT_ID) {
                assigned_id++;
                if (assigned_id == INVALID_AGENT_ID) {
                    LogMessage(LogLevel::ERROR, get_logger_source(), "CRITICAL: Agent ID counter wrap around."); return INVALID_AGENT_ID;
                }
            }
            next_available_agent_id_ = assigned_id + 1;
            auto [it, inserted] = entities_.try_emplace(assigned_id, entity);
            if (!inserted) {
                LogMessage(LogLevel::ERROR, get_logger_source(), "CRITICAL: Failed to insert entity with new ID " + std::to_string(assigned_id)); return INVALID_AGENT_ID;
            }
            entity->set_id(assigned_id);
            entity->set_event_bus(this);
            LogMessage(LogLevel::INFO, get_logger_source(), "Registered entity, assigned ID: " + std::to_string(assigned_id) + " (Type: " + typeid(*entity).name() + ")");
            return assigned_id;
        }

        void deregister_entity(AgentId id) {
            auto entity_it = entities_.find(id);
            if (entity_it == entities_.end()) {
                LogMessage(LogLevel::WARNING, get_logger_source(), "Deregister non-existent ID: " + std::to_string(id)); return;
            }
            ProcessorInterface* entity_ptr = entity_it->second;
            if (auto exact_subs = agent_exact_subscriptions_.find(id); exact_subs != agent_exact_subscriptions_.end()) {
                for (const std::string &topic : std::vector<std::string>(exact_subs->second.begin(), exact_subs->second.end())) unsubscribe(id, topic);
            }
            if (auto wc_subs = agent_wildcard_subscriptions_.find(id); wc_subs != agent_wildcard_subscriptions_.end()) {
                for (const std::string &topic : std::vector<std::string>(wc_subs->second.begin(), wc_subs->second.end())) unsubscribe(id, topic);
            }
            for (auto it = subscriber_stream_last_scheduled_ts_.begin(); it != subscriber_stream_last_scheduled_ts_.end(); ) {
                it = (it->first.second == id) ? subscriber_stream_last_scheduled_ts_.erase(it) : std::next(it);
            }
            if (entity_ptr) entity_ptr->set_event_bus(nullptr);
            entities_.erase(entity_it);
            LogMessage(LogLevel::INFO, get_logger_source(), "Deregistered entity ID: " + std::to_string(id) + (entity_ptr ? " ("+std::string(typeid(*entity_ptr).name())+")" : ""));
        }

        void subscribe(AgentId subscriber_id, const std::string &topic_str) {
            if (!entities_.count(subscriber_id)) {
                LogMessage(LogLevel::WARNING, get_logger_source(), "Subscribe ID " + std::to_string(subscriber_id) + " not registered. Topic: '" + topic_str + "'. Ignored.");
                return;
            }
            if (topic_str.empty()){ LogMessage(LogLevel::WARNING, get_logger_source(), "Sub " + std::to_string(subscriber_id) + " empty topic. Subscribing to root.");}

            size_t multi_level_pos = topic_str.find(MULTI_LEVEL_WILDCARD);
            if (multi_level_pos != std::string::npos) {
                auto parts = split_topic(topic_str);
                if (!parts.empty() && parts.back() != MULTI_LEVEL_WILDCARD && std::find(parts.begin(), parts.end(), MULTI_LEVEL_WILDCARD) != parts.end()) {
                    LogMessage(LogLevel::WARNING, get_logger_source(), "Invalid wildcard: '" + MULTI_LEVEL_WILDCARD + "' must be last: '" + topic_str + "'. Ignored."); return;
                }
            }

            if (is_wildcard_topic(topic_str)) {
                auto [_, inserted] = agent_wildcard_subscriptions_[subscriber_id].insert(topic_str);
                if (inserted) LogMessage(LogLevel::INFO, get_logger_source(), "Sub " + std::to_string(subscriber_id) + " wildcard topic '" + topic_str + "'");
                else LogMessage(LogLevel::DEBUG, get_logger_source(), "Sub " + std::to_string(subscriber_id) + " already wildcard sub for '" + topic_str + "'");
            } else {
                TrieNode *node = find_or_create_node(topic_str, true );
                if (!node) { LogMessage(LogLevel::ERROR, get_logger_source(), "Failed find/create Trie node for exact topic: '" + topic_str + "'. Sub failed for " + std::to_string(subscriber_id)); return; }
                auto [_, inserted] = node->subscribers.insert(subscriber_id);
                if (inserted) {
                    agent_exact_subscriptions_[subscriber_id].insert(topic_str);
                    LogMessage(LogLevel::INFO, get_logger_source(), "Sub " + std::to_string(subscriber_id) + " exact topic '" + topic_str + "' (NodeID: " + (node->topic_id == INVALID_ID_UINT64 ? "root" : get_topic_string(node->topic_id)) + ")");
                } else LogMessage(LogLevel::DEBUG, get_logger_source(), "Sub " + std::to_string(subscriber_id) + " already exact sub for '" + topic_str + "'");
            }
        }
        void unsubscribe(AgentId subscriber_id, const std::string &topic_str) {
            bool removed = false;
            if (is_wildcard_topic(topic_str)) {
                if (auto it = agent_wildcard_subscriptions_.find(subscriber_id); it != agent_wildcard_subscriptions_.end()) {
                    if (it->second.erase(topic_str) > 0) removed = true;
                    if (it->second.empty()) agent_wildcard_subscriptions_.erase(it);
                }
            } else {
                TrieNode *node = find_node(topic_str);
                if (node && node->subscribers.erase(subscriber_id) > 0) {
                    removed = true;
                    if (node->is_prunable()) prune_node_path(node);
                }
                if (auto it = agent_exact_subscriptions_.find(subscriber_id); it != agent_exact_subscriptions_.end()) {
                    if (it->second.erase(topic_str) > 0) removed = true; // Could be redundant if node->subscribers.erase worked
                    if (it->second.empty()) agent_exact_subscriptions_.erase(it);
                }
            }
            if (removed) LogMessage(LogLevel::INFO, get_logger_source(), "Unsub " + std::to_string(subscriber_id) + " from '" + topic_str + "'");
            else LogMessage(LogLevel::WARNING, get_logger_source(), "Unsub " + std::to_string(subscriber_id) + " from '" + topic_str + "', not found.");
        }


        template<typename E>
        void publish(
                AgentId publisher_id,
                const std::string &topic_str,
                const std::shared_ptr<const E> &event_ptr,
                const std::string &stream_id_str = ""
        ) {
            static_assert((std::is_same_v<E, EventTypes> || ...), "Event type E is not in the list of EventTypes for this EventBus.");

            if (is_wildcard_topic(topic_str)) {
                LogMessage(LogLevel::WARNING, get_logger_source(), "Publish to wildcard topic ('" + topic_str + "') not allowed. Ignored.");
                return;
            }
            if (!event_ptr) {
                LogMessage(LogLevel::WARNING, get_logger_source(), "Publish null event for topic: '" + topic_str + "'. Ignored.");
                return;
            }
            if (topic_str.empty()) { LogMessage(LogLevel::DEBUG, get_logger_source(), "Publishing to empty topic (root)."); }

            // --- MODIFICATION: Pre-Publish Hook Invocation ---
            TopicId interned_topic_id_for_hook = string_interner_.intern(topic_str);
            Timestamp publish_time_for_hook = current_time_;
            EventVariant event_variant_for_hook = event_ptr; // Construct variant once

            if (!pre_publish_hooks_.empty()) {
                for (PrePublishHookInterface* hook : pre_publish_hooks_) {
                    // Assuming hooks are non-null due to registration checks
                    try {
                        hook->on_pre_publish(
                                publisher_id,
                                interned_topic_id_for_hook,
                                event_variant_for_hook,
                                publish_time_for_hook,
                                this // Pass const pointer to the bus
                        );
                    } catch (const std::exception& e) {
                        LogMessage(LogLevel::ERROR, get_logger_source(),
                                   "Exception in pre-publish hook '" + hook->get_hook_name() +
                                   "' for topic '" + get_topic_string(interned_topic_id_for_hook) + "': " + e.what());
                    } catch (...) {
                        LogMessage(LogLevel::ERROR, get_logger_source(),
                                   "Unknown exception in pre-publish hook '" + hook->get_hook_name() +
                                   "' for topic '" + get_topic_string(interned_topic_id_for_hook) + "'.");
                    }
                }
            }
            // --- END MODIFICATION ---

            TopicId published_topic_id = interned_topic_id_for_hook; // Reuse interned ID
            StreamId stream_id = stream_id_str.empty() ? INVALID_ID_UINT64 : string_interner_.intern(stream_id_str);
            Timestamp original_publish_time = publish_time_for_hook; // Reuse time
            EventVariant event_variant = event_variant_for_hook; // Reuse variant

            std::unordered_set<AgentId> subscribers_to_notify;
            TrieNode *exact_node = find_node(topic_str);
            if (exact_node) {
                subscribers_to_notify.insert(exact_node->subscribers.begin(), exact_node->subscribers.end());
            }
            if (topic_str.empty() && exact_node == &topic_trie_root_) { // Special case for root if not handled by find_node
                subscribers_to_notify.insert(topic_trie_root_.subscribers.begin(), topic_trie_root_.subscribers.end());
            }

            for (const auto &[agent_id, wildcard_set] : agent_wildcard_subscriptions_) {
                if (subscribers_to_notify.count(agent_id)) continue; // Already added by exact match
                for (const std::string &pattern : wildcard_set) {
                    if (topic_matches_wildcard(pattern, topic_str)) {
                        subscribers_to_notify.insert(agent_id);
                        break;
                    }
                }
            }

            if (subscribers_to_notify.empty()) {
                LogMessage(LogLevel::DEBUG, get_logger_source(), "No subscribers for topic: '" + topic_str + "'. Event not queued.");
            }

            for (AgentId sub_id : subscribers_to_notify) {
                ProcessorInterface *receiver = entities_.count(sub_id) ? entities_.at(sub_id) : nullptr;
                if (!receiver) {
                    LogMessage(LogLevel::WARNING, get_logger_source(), "Sub ID " + std::to_string(sub_id) + " in sub lists but not entities. Dropping event for '" + topic_str + "'.");
                    continue;
                }

                Timestamp base_time_for_subscriber = original_publish_time;
                if (stream_id != INVALID_ID_UINT64) {
                    if (auto it = subscriber_stream_last_scheduled_ts_.find({stream_id, sub_id}); it != subscriber_stream_last_scheduled_ts_.end()) {
                        base_time_for_subscriber = std::max(base_time_for_subscriber, it->second);
                    }
                }

                const LatencyParameters* params = &default_latency_params_;
                if (auto it = inter_agent_latency_config_.find({publisher_id, sub_id}); it != inter_agent_latency_config_.end()) {
                    params = &it->second;
                }

                double raw_latency_us;
                if (params->dist_type == LatencyParameters::Type::FIXED) {
                    raw_latency_us = params->fixed_latency_us;
                } else {
                    std::lognormal_distribution<double> dist(params->get_lognormal_mu(), params->lognormal_sigma);
                    raw_latency_us = dist(random_engine_);
                }
                if (params->max_cap_us > 0) raw_latency_us = std::min(raw_latency_us, params->max_cap_us);
                raw_latency_us = std::max(1.0, raw_latency_us); // Min 1us

                Duration latency = std::chrono::duration_cast<Duration>(LatencyUnit(static_cast<long long>(raw_latency_us)));
                if (latency < Duration::zero()) latency = LatencyUnit(1);

                Timestamp final_scheduled_time = base_time_for_subscriber + latency;
                final_scheduled_time = std::max(final_scheduled_time, current_time_ + LatencyUnit(1)); // Must be at least 1us in future from current bus time

                SequenceNumber next_seq_num = ++global_schedule_sequence_counter_;
                ScheduledEvent scheduled_event{final_scheduled_time, event_variant, published_topic_id, publisher_id, sub_id, original_publish_time, stream_id, next_seq_num};

                if (stream_id != INVALID_ID_UINT64) {
                    subscriber_stream_last_scheduled_ts_[{stream_id, sub_id}] = final_scheduled_time;
                }

                if (receiver->is_processing()) {
                    LogMessage(LogLevel::DEBUG, get_logger_source(), "Queueing re-entrant event for busy Agent " + std::to_string(sub_id) + " (Topic: " + get_topic_string(published_topic_id) + ", Seq: " + std::to_string(next_seq_num) + ")");
                    receiver->queue_reentrant_event(std::move(scheduled_event));
                } else {
                    event_queue_.push(std::move(scheduled_event));
                }
            }
        }

        std::optional<ScheduledEvent> peak() const {
            if (event_queue_.empty()) return std::nullopt;
            return event_queue_.top();
        }

        std::optional<ScheduledEvent> step() {
            if (event_queue_.empty()) return std::nullopt;

            ScheduledEvent current_event = event_queue_.top();
            event_queue_.pop();

            if (current_event.scheduled_time < current_time_) {
                LogMessage(LogLevel::ERROR, get_logger_source(), "CRITICAL: Popped event scheduled BEFORE current_bus_time. Event Topic: '" + get_topic_string(current_event.topic) + "', Seq: " + std::to_string(current_event.sequence_number) + ". Advancing bus time.");
            }
            current_time_ = current_event.scheduled_time;

            auto entity_it = entities_.find(current_event.subscriber_id);
            if (entity_it == entities_.end()) {
                LogMessage(LogLevel::INFO, get_logger_source(), "Dropping event for deregistered sub ID: " + std::to_string(current_event.subscriber_id) + " on topic '" + get_topic_string(current_event.topic) + "' (Seq: " + std::to_string(current_event.sequence_number) + ")");
                return current_event;
            }
            ProcessorInterface *receiver = entity_it->second;

            if (LoggerConfig::G_CURRENT_LOG_LEVEL <= LogLevel::DEBUG) {
                std::ostringstream oss;
                oss << "Processing Event for Agent " << current_event.subscriber_id << " (Seq: " << current_event.sequence_number << ")\n"
                    << "  Time: " << format_timestamp(current_time_) << " (PubAt: " << format_timestamp(current_event.publish_time) << ")\n"
                    << "  PubID: " << current_event.publisher_id << ", SubID: " << current_event.subscriber_id << "\n"
                    << "  Topic: '" << get_topic_string(current_event.topic) << "' (ID: " << current_event.topic << ")\n"
                    << "  Stream: '" << get_stream_string(current_event.stream_id) << "' (ID: " << current_event.stream_id << ")\n"
                    << "  Event Type: ";
                std::visit([&oss](const auto& ev_ptr){ oss << typeid(*ev_ptr).name(); }, current_event.event);
                LogMessage(LogLevel::DEBUG, get_logger_source(), oss.str());
            }

            class ProcessingGuard {
                ProcessorInterface* p_; bool S_;
            public:
                ProcessingGuard(ProcessorInterface* p) : p_(p), S_(false) { if(p_) {S_=p_->is_processing(); p_->set_processing(true);}}
                ~ProcessingGuard() { if(p_) p_->set_processing(S_); }
                ProcessingGuard(const ProcessingGuard&) = delete; ProcessingGuard& operator=(const ProcessingGuard&) = delete;
            };

            try {
                ProcessingGuard guard(receiver);
                receiver->process_event_variant(current_event.event, current_event.topic, current_event.publisher_id, current_time_, current_event.stream_id, current_event.sequence_number);
            } catch (const std::exception &e) {
                LogMessage(LogLevel::ERROR, get_logger_source(), "Exception during event processing for agent " + std::to_string(current_event.subscriber_id) + ": " + e.what());
            } catch (...) {
                LogMessage(LogLevel::ERROR, get_logger_source(), "Unknown exception during event processing for agent " + std::to_string(current_event.subscriber_id));
            }

            if (receiver) receiver->flush_streams();
            return current_event;
        }

        void reschedule_event(ScheduledEvent &&event) {
            LogMessage(LogLevel::DEBUG, get_logger_source(), "Re-scheduling event for agent " + std::to_string(event.subscriber_id) + " (Seq: " + std::to_string(event.sequence_number) + ")");
            event_queue_.push(std::move(event));
        }

        template<typename E>
        void schedule_at(
                AgentId publisher_id,
                AgentId subscriber_id,
                const std::string& topic_str,
                const std::shared_ptr<const E>& event_ptr,
                Timestamp target_execution_time,
                const std::string& stream_id_str = ""
        ) {
            static_assert((std::is_same_v<E, EventTypes> || ...), "Scheduled event type E not in EventVariant list");
            if (!event_ptr) { LogMessage(LogLevel::WARNING, get_logger_source(), "schedule_at: null event_ptr for topic '" + topic_str + "'. Ignoring."); return; }
            if (!entities_.count(subscriber_id)) { LogMessage(LogLevel::WARNING, get_logger_source(), "schedule_at: sub " + std::to_string(subscriber_id) + " not found. Ignoring."); return; }

            TopicId topic_id = string_interner_.intern(topic_str);
            StreamId stream_id = stream_id_str.empty() ? INVALID_ID_UINT64 : string_interner_.intern(stream_id_str);
            Timestamp call_time = current_time_;
            Timestamp final_time = target_execution_time;
            const Duration min_future = LatencyUnit(1);

            final_time = std::max(final_time, call_time + min_future);
            if (stream_id != INVALID_ID_UINT64) {
                if (auto it = subscriber_stream_last_scheduled_ts_.find({stream_id, subscriber_id}); it != subscriber_stream_last_scheduled_ts_.end()) {
                    final_time = std::max(final_time, it->second + min_future);
                }
            }
            SequenceNumber seq_num = ++global_schedule_sequence_counter_;
            ScheduledEvent sev{final_time, event_ptr, topic_id, publisher_id, subscriber_id, call_time, stream_id, seq_num};
            if (stream_id != INVALID_ID_UINT64) subscriber_stream_last_scheduled_ts_[{stream_id, subscriber_id}] = final_time;
            event_queue_.push(std::move(sev));
            LogMessage(LogLevel::DEBUG, get_logger_source(), "Scheduled event via schedule_at for Agent " + std::to_string(subscriber_id) + " (Topic: '" + topic_str + "', FinalTime: " + format_timestamp(final_time) + ", Seq: " + std::to_string(seq_num) + ")");
        }

        Timestamp get_current_time() const { return current_time_; }
        const std::string &get_topic_string(TopicId id) const { return string_interner_.resolve(id); }
        const std::string &get_stream_string(StreamId id) const { return string_interner_.resolve(id); }
        TopicId intern_topic(const std::string &topic_str) { return string_interner_.intern(topic_str); }
        StreamId intern_stream(const std::string &stream_str) { return string_interner_.intern(stream_str); }
        size_t get_event_queue_size() const { return event_queue_.size(); }

        std::string format_timestamp(Timestamp ts) const {
            auto count_us = std::chrono::duration_cast<std::chrono::microseconds>(ts.time_since_epoch()).count();
            return std::to_string(count_us) + "us";
        }
    };

} // namespace EventBusSystem