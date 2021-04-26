/* Copyright 2021 Enjin Pte Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "enjinsdk/PusherEventService.hpp"

#include "AssetChannel.hpp"
#include "EnumUtils.hpp"
#include "EventTypeDef.hpp"
#include "ISubscriptionEventListener.hpp"
#include "PlayerChannel.hpp"
#include "ProjectChannel.hpp"
#include "PusherClient.hpp"
#include "PusherEvent.hpp"
#include "WalletChannel.hpp"
#include "enjinsdk_utils/StringUtils.hpp"
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <sstream>
#include <utility>

#ifndef ENJINSDK_INCLUDE_WEBSOCKET_CLIENT_IMPL
#define ENJINSDK_INCLUDE_WEBSOCKET_CLIENT_IMPL 0
#endif
#if ENJINSDK_INCLUDE_WEBSOCKET_CLIENT_IMPL

#include "WebsocketClientImpl.hpp"

#endif

namespace enjin::sdk::events {

class PusherEventService::PusherEventListener : public pusher::ISubscriptionEventListener {
public:
    PusherEventListener() = delete;

    explicit PusherEventListener(PusherEventService& service) : service(&service) {
    }

    ~PusherEventListener() override = default;

    void on_event(const pusher::PusherEvent& event) override {
        const std::string& key = event.get_event_name().has_value()
                                 ? event.get_event_name().value()
                                 : std::string();
        const std::string& channel = event.get_channel_name().has_value()
                                     ? event.get_channel_name().value()
                                     : std::string();
        const std::string& message = event.get_data().has_value()
                                     ? event.get_data().value()
                                     : std::string();

        // Log event received
        if (service->logger_provider != nullptr) {
            std::stringstream ss;
            ss << "Received event " << key << " on channel " << channel << " with results " << message;
            service->logger_provider->log(utils::LogLevel::INFO, ss.str());
        }

        if (service->listeners.empty()) {
            if (service->logger_provider != nullptr) {
                service->logger_provider->log(utils::LogLevel::INFO, "No registered listener when event was received");
            }

            return;
        }

        EventTypeDef def = EventTypeDef::get_from_key(key);
        if (def.get_type() == models::EventType::UNKNOWN) {
            if (service->logger_provider != nullptr) {
                std::stringstream ss;
                ss << "Unknown event type for key " << def.get_key();
                service->logger_provider->log(utils::LogLevel::WARN, ss.str());
            }

            return;
        }

        models::NotificationEvent notification_event(def.get_type(), channel, message);

        for (auto& registration : service->listeners) {
            if (registration->get_matcher()(notification_event.get_type())) {
                registration->get_listener().notification_received(notification_event);
            }
        }
    }

private:
    PusherEventService* service;
};

class PusherEventServiceImpl {
public:
    PusherEventServiceImpl() = delete;

    explicit PusherEventServiceImpl(std::shared_ptr<websockets::IWebsocketClient> ws_client)
            : ws_client(std::move(ws_client)),
              client(nullptr) {
    }

    ~PusherEventServiceImpl() = default;

    void init_client(const std::string& key,
                     const pusher::PusherOptions& options,
                     const std::shared_ptr<utils::LoggerProvider>& logger_provider = nullptr) {
        client = std::make_unique<pusher::PusherClient>(ws_client, key, options, logger_provider);

        client->set_on_connection_state_change_handler([this, logger_provider](pusher::ConnectionState state) {
            if (logger_provider != nullptr && state != pusher::ConnectionState::ALL) {
                std::stringstream ss;
                ss << "Pusher client " << enjin::utils::to_lower(utils::serialize_pusher_connection_state(state));
                logger_provider->log(utils::LogLevel::INFO, ss.str());
            }

            if (state == pusher::ConnectionState::CONNECTED && connected_handler.has_value()) {
                connected_handler.value()();
            } else if (state == pusher::ConnectionState::DISCONNECTED && disconnected_handler.has_value()) {
                disconnected_handler.value()();
            }
        });

        client->set_on_error_handler([this, logger_provider](const std::exception& e) {
            if (logger_provider != nullptr) {
                std::stringstream ss;
                ss << "Error on Pusher client: " << e.what();
                logger_provider->log(utils::LogLevel::SEVERE, ss.str());
            }

            if (error_handler.has_value()) {
                error_handler.value()(e);
            }
        });

        client->connect();
    }

    [[nodiscard]] const std::unique_ptr<pusher::PusherClient>& get_client() const {
        return client;
    }

    void set_connected_handler(const std::function<void()>& handler) {
        connected_handler = handler;
    }

    void set_disconnected_handler(const std::function<void()>& handler) {
        disconnected_handler = handler;
    }

    void set_error_handler(const std::function<void(const std::exception&)>& handler) {
        error_handler = handler;
    }

private:
    std::shared_ptr<websockets::IWebsocketClient> ws_client;
    std::unique_ptr<pusher::PusherClient> client;

    // Handlers
    std::optional<std::function<void()>> connected_handler;
    std::optional<std::function<void()>> disconnected_handler;
    std::optional<std::function<void(const std::exception&)>> error_handler;
};

PusherEventService::PusherEventService(std::unique_ptr<websockets::IWebsocketClient> ws_client,
                                       std::shared_ptr<utils::LoggerProvider> logger_provider)
        : impl(new PusherEventServiceImpl(std::move(ws_client))),
          logger_provider(std::move(logger_provider)) {
}

PusherEventService::PusherEventService(std::unique_ptr<websockets::IWebsocketClient> ws_client,
                                       std::shared_ptr<utils::LoggerProvider> logger_provider,
                                       models::Platform platform)
        : impl(new PusherEventServiceImpl(std::move(ws_client))),
          platform(std::move(platform)),
          logger_provider(std::move(logger_provider)) {
}

PusherEventService::~PusherEventService() {
    delete impl;
}

void PusherEventService::start() {
    shutdown();

    std::optional<models::Notifications> notifications = platform->get_notifications();
    if (!notifications.has_value()) {
        return;
    }

    std::optional<models::Pusher> pusher = notifications->get_pusher();
    if (!pusher.has_value() || !pusher->get_options().has_value()) {
        return;
    }

    std::optional<std::string> key = pusher->get_key();
    std::optional<std::string> cluster = pusher->get_options()->get_cluster();
    std::optional<bool> encrypted = pusher->get_options()->get_encrypted();
    if (!key.has_value() || key->empty() || !cluster.has_value() || cluster->empty()) {
        return;
    }

    pusher::PusherOptions options = pusher::PusherOptions()
            .set_cluster(cluster.value())
            .set_encrypted(encrypted.value());

    listener = std::make_shared<PusherEventListener>(*this);
    impl->init_client(key.value(), options, logger_provider);
}

void PusherEventService::start(models::Platform platform) {
    PusherEventService::platform = std::move(platform);
    start();
}

void PusherEventService::shutdown() {
    auto& pusher_client = impl->get_client();
    if (pusher_client != nullptr) {
        pusher_client->disconnect();
    }
}

bool PusherEventService::is_connected() const {
    auto& pusher_client = impl->get_client();
    return pusher_client != nullptr && pusher_client->get_state() == pusher::ConnectionState::CONNECTED;
}

bool PusherEventService::is_registered(IEventListener& listener) const {
    auto loc = std::find_if(listeners.begin(),
                            listeners.end(),
                            [&listener](const std::shared_ptr<EventListenerRegistration>& r) {
                                return &r->get_listener() == &listener;
                            });
    return loc != listeners.end();
}

void PusherEventService::set_connected_handler(const std::function<void()>& handler) {
    impl->set_connected_handler(handler);
}

void PusherEventService::set_disconnected_handler(const std::function<void()>& handler) {
    impl->set_disconnected_handler(handler);
}

void PusherEventService::set_error_handler(const std::function<void(const std::exception&)>& handler) {
    impl->set_error_handler(handler);
}

std::shared_ptr<EventListenerRegistration>
PusherEventService::register_listener(std::shared_ptr<IEventListener> listener) {
    return cache_registration(EventListenerRegistration::RegistrationListenerConfiguration(listener));
}

std::shared_ptr<EventListenerRegistration>
PusherEventService::register_listener_with_matcher(std::shared_ptr<IEventListener> listener,
                                                   std::function<bool(models::EventType)> matcher) {
    return cache_registration(EventListenerRegistration::RegistrationListenerConfiguration(listener)
                                      .with_matcher(matcher));
}

std::shared_ptr<EventListenerRegistration>
PusherEventService::register_listener_including_types(std::shared_ptr<IEventListener> listener,
                                                      const std::vector<models::EventType>& types) {
    return cache_registration(EventListenerRegistration::RegistrationListenerConfiguration(listener)
                                      .with_allowed_events(types));
}

std::shared_ptr<EventListenerRegistration>
PusherEventService::register_listener_excluding_types(std::shared_ptr<IEventListener> listener,
                                                      const std::vector<models::EventType>& types) {
    return cache_registration(EventListenerRegistration::RegistrationListenerConfiguration(listener)
                                      .with_ignored_events(types));
}

std::shared_ptr<EventListenerRegistration>
PusherEventService::cache_registration(EventListenerRegistration::RegistrationListenerConfiguration configuration) {
    // Check if a registration for the listener already exists
    for (const auto& r : listeners) {
        if (&r->get_listener() == &configuration.get_listener()) {
            return r;
        }
    }

    std::shared_ptr<EventListenerRegistration> registration = configuration.create();
    listeners.push_back(registration);

    return registration;
}

void PusherEventService::unregister_listener(IEventListener& listener) {
    // Finds the first registration that matches
    auto loc = std::find_if(listeners.begin(),
                            listeners.end(),
                            [&listener](const std::shared_ptr<EventListenerRegistration>& r) {
                                return &r->get_listener() == &listener;
                            });

    if (loc != listeners.end()) {
        listeners.erase(loc);
    }
}

void PusherEventService::subscribe_to_project(int project) {
    subscribe(ProjectChannel(platform.value(), project).channel());
}

void PusherEventService::unsubscribe_to_project(int project) {
    unsubscribe(ProjectChannel(platform.value(), project).channel());
}

bool PusherEventService::is_subscribed_to_project(int project) const {
    return impl->get_client()->is_subscribed(ProjectChannel(platform.value(), project).channel());
}

void PusherEventService::subscribe_to_player(int project, const std::string& player) {
    subscribe(PlayerChannel(platform.value(), project, player).channel());
}

void PusherEventService::unsubscribe_to_player(int project, const std::string& player) {
    unsubscribe(PlayerChannel(platform.value(), project, player).channel());
}

bool PusherEventService::is_subscribed_to_player(int project, const std::string& player) const {
    return impl->get_client()->is_subscribed(PlayerChannel(platform.value(), project, player).channel());
}

void PusherEventService::subscribe_to_asset(const std::string& asset) {
    subscribe(AssetChannel(platform.value(), asset).channel());
}

void PusherEventService::unsubscribe_to_asset(const std::string& asset) {
    unsubscribe(AssetChannel(platform.value(), asset).channel());
}

bool PusherEventService::is_subscribed_to_asset(const std::string& asset) const {
    return impl->get_client()->is_subscribed(AssetChannel(platform.value(), asset).channel());
}

void PusherEventService::subscribe_to_wallet(const std::string& wallet) {
    subscribe(WalletChannel(platform.value(), wallet).channel());
}

void PusherEventService::unsubscribe_to_wallet(const std::string& wallet) {
    unsubscribe(WalletChannel(platform.value(), wallet).channel());
}

bool PusherEventService::is_subscribed_to_wallet(const std::string& wallet) const {
    return impl->get_client()->is_subscribed(WalletChannel(platform.value(), wallet).channel());
}

void PusherEventService::subscribe(const std::string& channel) {
    auto& pusher_client = impl->get_client();
    if (pusher_client == nullptr || pusher_client->is_subscribed_or_pending(channel)) {
        return;
    }

    pusher_client->subscribe(channel);
    bind(channel);
}

void PusherEventService::unsubscribe(const std::string& channel) {
    auto& pusher_client = impl->get_client();
    if (pusher_client == nullptr || !pusher_client->is_subscribed(channel)) {
        return;
    }

    pusher_client->unsubscribe(channel);
}

void PusherEventService::bind(const std::string& channel) {
    auto& pusher_client = impl->get_client();
    for (auto& def : EventTypeDef::filter_by_channel_type({channel})) {
        pusher_client->bind(def.get_key(), listener);
    }
}

const std::shared_ptr<utils::LoggerProvider>& PusherEventService::get_logger_provider() const {
    return logger_provider;
}

std::unique_ptr<PusherEventService> PusherEventServiceBuilder::build() {
    if (m_ws_client == nullptr) {
#if ENJINSDK_INCLUDE_WEBSOCKET_CLIENT_IMPL
        m_ws_client = std::make_unique<websockets::WebsocketClientImpl>(m_provider);
#else
        throw std::runtime_error("Attempted building Pusher event service without providing a websocket client");
#endif
    }

    return m_platform.has_value()
           ? std::unique_ptr<PusherEventService>(new PusherEventService(std::move(m_ws_client),
                                                                        m_provider,
                                                                        m_platform.value()))
           : std::unique_ptr<PusherEventService>(new PusherEventService(std::move(m_ws_client),
                                                                        m_provider));
}

PusherEventServiceBuilder& PusherEventServiceBuilder::platform(const models::Platform& platform) {
    m_platform = platform;
    return *this;
}

PusherEventServiceBuilder&
PusherEventServiceBuilder::ws_client(std::unique_ptr<websockets::IWebsocketClient> ws_client) {
    m_ws_client = std::move(ws_client);
    return *this;
}

PusherEventServiceBuilder& PusherEventServiceBuilder::logger_provider(std::shared_ptr<utils::LoggerProvider> provider) {
    m_provider = std::move(provider);
    return *this;
}

}
