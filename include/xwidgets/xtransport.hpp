/***************************************************************************
* Copyright (c) 2017, Sylvain Corlay and Johan Mabille                     *
*                                                                          *
* Distributed under the terms of the BSD 3-Clause License.                 *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
****************************************************************************/

#ifndef XWIDGETS_TRANSPORT_HPP
#define XWIDGETS_TRANSPORT_HPP

#include <functional>
#include <list>
#include <string>
#include <type_traits>
#include <utility>

#include "xeus/xcomm.hpp"
#include "xeus/xinterpreter.hpp"

#include "xwidgets_config.hpp"

using namespace std::placeholders;

namespace xeus
{

    /**********************************
     * Comm target handling functions *
     **********************************/

    inline const char* get_widget_target_name()
    {
        return "jupyter.widget";
    }

    inline void xobject_comm_opened(const xcomm&, const xmessage&)
    {
    }

    inline int register_widget_target()
    {
        xeus::get_interpreter()
          .comm_manager()
          .register_comm_target(get_widget_target_name(), xeus::xobject_comm_opened);
        return 0;
    }

    inline xtarget* get_widget_target()
    {
        static int registered = register_widget_target();
        return ::xeus::get_interpreter()
          .comm_manager()
          .target(::xeus::get_widget_target_name());
    }

    inline const char* get_widget_protocol_version()
    {
        return "2.0.0";
    }

    /*******************************
     * base xtransport declaration *
     *******************************/

    template <class D>
    class xtransport
    {
    public:

        using message_callback_type = std::function<void(const xjson&)>;

        using derived_type = D;

        xtransport();
        xtransport(const xtransport&);
        xtransport(xtransport&&);
        xtransport& operator=(const xtransport&);
        xtransport& operator=(xtransport&&);

        derived_type& derived_cast() & noexcept;
        const derived_type& derived_cast() const & noexcept;
        derived_type derived_cast() && noexcept;

        xguid id() const noexcept;
        void display() const;

        void send_patch(xjson&& state) const;
        void send(xjson&& content) const;

        void on_message(message_callback_type);

    protected:
        
        bool moved_from() const noexcept;
        void open();
        void close();

        template <class P>
        void notify(const P& property) const;

    private:
    
        void handle_message(const xmessage& message);
        void handle_custom_message(const xjson& content);

        bool m_moved_from;
        std::list<message_callback_type> m_message_callbacks;
        const xjson* m_hold;
        xcomm m_comm;
    };
    
    template <class D>
    void to_json(xjson& j, const xtransport<D>& o);

    template <class D>
    inline void from_json(const xjson& j, xtransport<D>& o);

    /**********************************
     * base xtransport implementation *
     **********************************/

    template <class D>
    inline xtransport<D>::xtransport()
        : m_moved_from(false),
          m_hold(nullptr), 
          m_comm(::xeus::get_widget_target(), xguid())
    {
        m_comm.on_message(std::bind(&xtransport::handle_message, this, _1));
    }

    template <class D>
    inline xtransport<D>::xtransport(const xtransport& other)
        : m_moved_from(false),
          m_message_callbacks(other.m_message_callbacks),
          m_hold(other.m_hold),
          m_comm(other.m_comm)
    {
        m_comm.on_message(std::bind(&xtransport::handle_message, this, _1));
    }

    template <class D>
    inline xtransport<D>::xtransport(xtransport&& other)
        : m_moved_from(false),
          m_message_callbacks(std::move(other.m_message_callbacks)),
          m_hold(std::move(other.m_hold)),
          m_comm(std::move(other.m_comm))
    {
        other.m_moved_from = true;
        m_comm.on_message(std::bind(&xtransport::handle_message, this, _1));
    }

    template <class D>
    inline xtransport<D>& xtransport<D>::operator=(const xtransport& other)
    {
        m_moved_from = false;
        m_message_callbacks = other.m_message_callbacks;
        m_hold = other.m_hold;
        m_comm = other.m_comm;
        m_comm.on_message(std::bind(&xtransport::handle_message, this, _1));
        return *this;
    }

    template <class D>
    inline xtransport<D>& xtransport<D>::operator=(xtransport&& other)
    {
        other.m_moved_from = true;
        m_moved_from = false;
        m_message_callbacks = std::move(other.m_message_callbacks);
        m_hold = std::move(other.m_hold);
        m_comm = std::move(other.m_comm);
        m_comm.on_message(std::bind(&xtransport::handle_message, this, _1));
        return *this;
    }

    template <class D>
    inline auto xtransport<D>::derived_cast() & noexcept -> derived_type&
    {
        return *static_cast<derived_type*>(this);
    }

    template <class D>
    inline auto xtransport<D>::derived_cast() const& noexcept -> const derived_type& 
    {
        return *static_cast<const derived_type*>(this);
    }
    
    template <class D>    
    inline auto xtransport<D>::derived_cast() && noexcept -> derived_type
    {
        return *static_cast<derived_type*>(this);
    }

    template <class D>
    inline auto xtransport<D>::id() const noexcept -> xguid
    {
        return m_comm.id();
    }

    template <class D>
    inline void xtransport<D>::display() const
    {
        xeus::xjson mime_bundle;

        // application/vnd.jupyter.widget-view+json
        xeus::xjson widgets_json;
        widgets_json["version_major"] = "2";
        widgets_json["version_minor"] = "0";
        widgets_json["model_id"] = xeus::guid_to_hex(this->derived_cast().id());
        mime_bundle["application/vnd.jupyter.widget-view+json"] = std::move(widgets_json);

        // text/plain
        mime_bundle["text/plain"] = "A Jupyter widget";

        ::xeus::get_interpreter().display_data(
            std::move(mime_bundle),
            xeus::xjson::object(),
            xeus::xjson::object()
        );
    }

    template <class D>
    template <class P>
    inline void xtransport<D>::notify(const P& property) const
    {
        if (m_hold != nullptr)
        {
            auto it = m_hold->find(property.name());
            if (it != m_hold->end() && it.value() == property())
            {
                return;
            }
        }
        xjson state;
        state[property.name()] = property();
        send_patch(std::move(state));
    }

    template <class D>
    inline void xtransport<D>::send_patch(xjson&& patch) const
    {
        xjson metadata;
        metadata["version"] = get_widget_protocol_version();
        xjson data;
        data["method"] = "update";
        data["state"] = std::move(patch);
        m_comm.send(std::move(metadata), std::move(data));
    }

    template <class D>
    inline void xtransport<D>::send(xjson&& content) const
    {
        xjson metadata;
        metadata["version"] = get_widget_protocol_version();
        xjson data;
        data["method"] = "custom";
        data["content"] = std::move(content);
        m_comm.send(std::move(metadata), std::move(data));
    }

    template <class D>
    inline void xtransport<D>::on_message(message_callback_type cb)
    {
         m_message_callbacks.emplace_back(std::move(cb));
    }

    template <class D>
    inline bool xtransport<D>::moved_from() const noexcept
    {
         return m_moved_from;
    }

    template <class D>
    inline void xtransport<D>::open()
    {
        xjson metadata;
        metadata["version"] = get_widget_protocol_version();
        xeus::xjson data;
        data["state"] = derived_cast().get_state();
        m_comm.open(std::move(metadata), std::move(data));
    }

    template <class D>
    inline void xtransport<D>::close()
    {
        m_comm.close(xjson::object(), xjson::object());
    }

    template <class D>
    inline void xtransport<D>::handle_message(const xmessage& message)
    {
        const xjson& content = message.content();
        const xjson& data = content["data"];
        std::string method = data["method"];
        if (method == "update")
        {
            auto it = data.find("state");
            if (it != data.end())
            {
                derived_cast().apply_patch(it.value());
            }
        }
        else if (method == "request_state")
        {
            send_patch(derived_cast().get_state());
        }
        else if (method == "custom")
        {
            auto it = data.find("content");
            if (it != data.end())
            {
                handle_custom_message(it.value());
            }
        }
    }

    template <class D>
    inline void xtransport<D>::handle_custom_message(const xjson& content)
    {
        for (auto it = m_message_callbacks.begin(); it != m_message_callbacks.end(); ++it)
        {
            it->operator()(content);
        }
    }
}

#endif
