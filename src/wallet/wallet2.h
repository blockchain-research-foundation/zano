// Copyright (c) 2014-2018 Zano Project
// Copyright (c) 2014-2018 The Louisdor Project
// Copyright (c) 2012-2013 The Cryptonote developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once
#include <memory>
#include <boost/serialization/list.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/deque.hpp>
#include <boost/serialization/singleton.hpp>
#include <boost/serialization/extended_type_info.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include <atomic>

#include "include_base_utils.h"
#include "profile_tools.h"
#include "sync_locked_object.h"


#include "currency_core/currency_boost_serialization.h"
#include "currency_core/account_boost_serialization.h"
#include "currency_core/currency_format_utils.h"

#include "wallet_rpc_server_commans_defs.h"
#include "currency_core/currency_format_utils.h"
#include "common/unordered_containers_boost_serialization.h"
#include "storages/portable_storage_template_helper.h"
#include "crypto/chacha8.h"
#include "crypto/hash.h"
#include "core_rpc_proxy.h"
#include "core_default_rpc_proxy.h"
#include "wallet_errors.h"
#include "eos/portable_archive.hpp"
#include "currency_core/core_runtime_config.h"
#include "currency_core/offers_services_helpers.h"
#include "currency_core/bc_offers_serialization.h"
#include "currency_core/bc_escrow_service.h"


#define WALLET_DEFAULT_TX_SPENDABLE_AGE                               10
#define WALLET_POS_MINT_CHECK_HEIGHT_INTERVAL                         1

#undef LOG_DEFAULT_CHANNEL 
#define LOG_DEFAULT_CHANNEL "wallet"
ENABLE_CHANNEL_BY_DEFAULT("wallet");

namespace tools
{
#pragma pack(push, 1)
  struct wallet_file_binary_header
  {
    uint64_t m_signature;
    uint16_t m_cb_keys;
    uint64_t m_cb_body;
  };
#pragma pack (pop)


  struct money_transfer2_details
  {
    std::vector<size_t> receive_indices;
    std::vector<size_t> spent_indices;
  };


  class i_wallet2_callback
  {
  public:
    virtual void on_new_block(uint64_t /*height*/, const currency::block& /*block*/) {}
    virtual void on_transfer2(const wallet_rpc::wallet_transfer_info& wti, uint64_t balance, uint64_t unlocked_balance, uint64_t total_mined) {}
    virtual void on_pos_block_found(const currency::block& /*block*/) {}
    virtual void on_sync_progress(const uint64_t& /*percents*/) {}
    virtual void on_transfer_canceled(const wallet_rpc::wallet_transfer_info& wti) {}
  };

  struct tx_dust_policy
  {
    uint64_t dust_threshold;
    bool add_to_fee;
    currency::account_public_address addr_for_dust;

    tx_dust_policy(uint64_t a_dust_threshold = DEFAULT_DUST_THRESHOLD, bool an_add_to_fee = true, currency::account_public_address an_addr_for_dust = currency::account_public_address())
      : dust_threshold(a_dust_threshold)
      , add_to_fee(an_add_to_fee)
      , addr_for_dust(an_addr_for_dust)
    {
    }
  };  






  class test_generator;

  class wallet2
  {
    wallet2(const wallet2&) : m_stop(false),
                              m_wcallback(new i_wallet2_callback()), 
                              m_height_of_start_sync(0), 
                              m_last_sync_percent(0), 
                              m_do_rise_transfer(false)
    {};
  public:
    wallet2() : m_stop(false), 
                m_wcallback(new i_wallet2_callback()), //stub
                m_core_proxy(new default_http_core_proxy()), 
                m_upper_transaction_size_limit(0), 
                m_height_of_start_sync(0), 
                m_last_sync_percent(0), 
                m_fake_outputs_count(0),
                m_do_rise_transfer(false)
    {
      m_core_runtime_config = currency::get_default_core_runtime_config();
    };

#define   WALLET_TRANSFER_DETAIL_FLAG_SPENT                            0x00000001
#define   WALLET_TRANSFER_DETAIL_FLAG_BLOCKED                          0x00000002       
#define   WALLET_TRANSFER_DETAIL_FLAG_ESCROW_PROPOSAL_RESERVATION      0x00000004
#define   WALLET_TRANSFER_DETAIL_FLAG_MINED_TRANSFER                   0x00000008
    
    static std::string transform_tx_to_str(const currency::transaction& tx);
    static currency::transaction transform_str_to_tx(const std::string& tx_str);



    struct transaction_wallet_info
    {
      uint64_t m_block_height;
      uint64_t m_block_timestamp;
      currency::transaction m_tx;

      BEGIN_KV_SERIALIZE_MAP()
        KV_SERIALIZE(m_block_height)
        KV_SERIALIZE(m_block_timestamp)
        KV_SERIALIZE_CUSTOM(m_tx, std::string, tools::wallet2::transform_tx_to_str, tools::wallet2::transform_str_to_tx)
      END_KV_SERIALIZE_MAP()
    };

    static const transaction_wallet_info& transform_ptr_to_value(const std::shared_ptr<transaction_wallet_info>& a);
    static std::shared_ptr<transaction_wallet_info> transform_value_to_ptr(const transaction_wallet_info& d);
    
    struct transfer_details_base;
    static uint64_t transfer_details_base_to_amount(const transfer_details_base& tdb);
    static std::string transfer_details_base_to_tx_hash(const transfer_details_base& tdb);

    struct transfer_details_base
    {
      std::shared_ptr<transaction_wallet_info> m_ptx_wallet_info;
      uint64_t m_internal_output_index;
      uint64_t m_spent_height;
      uint32_t m_flags;

      uint64_t amount() const { return m_ptx_wallet_info->m_tx.vout[m_internal_output_index].amount; }
      bool is_spent() const { return m_flags & WALLET_TRANSFER_DETAIL_FLAG_SPENT; }

      BEGIN_KV_SERIALIZE_MAP()
        //KV_SERIALIZE(*m_ptx_wallet_info)
        KV_SERIALIZE_CUSTOM(m_ptx_wallet_info, const transaction_wallet_info&, tools::wallet2::transform_ptr_to_value, tools::wallet2::transform_value_to_ptr)
        KV_SERIALIZE(m_internal_output_index)
        KV_SERIALIZE(m_spent_height)
        KV_SERIALIZE(m_flags)
        KV_SERIALIZE_EPHEMERAL_N(uint64_t, tools::wallet2::transfer_details_base_to_amount, "amount")
        KV_SERIALIZE_EPHEMERAL_N(std::string, tools::wallet2::transfer_details_base_to_tx_hash, "tx_id")
      END_KV_SERIALIZE_MAP()

    };


    struct transfer_details : public transfer_details_base
    {
      uint64_t m_global_output_index;
      crypto::key_image m_key_image; //TODO: key_image stored twice :(

      BEGIN_KV_SERIALIZE_MAP()
        KV_SERIALIZE(m_global_output_index)
        KV_SERIALIZE_POD_AS_HEX_STRING(m_key_image)
        KV_CHAIN_BASE(transfer_details_base)
      END_KV_SERIALIZE_MAP()
    };




    struct payment_details
    {
      crypto::hash m_tx_hash;
      uint64_t m_amount;
      uint64_t m_block_height;
      uint64_t m_unlock_time;
    };

    struct mining_context
    {
      currency::COMMAND_RPC_SCAN_POS::request sp;
      currency::COMMAND_RPC_SCAN_POS::response rsp;
      currency::wide_difficulty_type basic_diff;
      currency::stake_modifier_type sm;
    };

    struct expiration_entry_info
    {
      std::vector<uint64_t> selected_transfers;
      uint64_t change_amount;
      uint64_t expiration_time;
      crypto::hash related_tx_id; // tx id which caused money lock, if any (ex: escrow proposal transport tx)
    };



    typedef std::unordered_multimap<std::string, payment_details> payment_container;

    typedef std::deque<transfer_details> transfer_container;
    typedef std::unordered_map<crypto::hash, transfer_details_base> multisig_transfer_container;
    typedef std::unordered_map<crypto::hash, tools::wallet_rpc::escrow_contract_details_basic> escrow_contracts_container;
    typedef std::map<uint64_t, std::set<size_t> > free_amounts_cache_type;


    struct keys_file_data
    {
      crypto::chacha8_iv iv;
      std::string account_data;

      BEGIN_SERIALIZE_OBJECT()
        FIELD(iv)
        FIELD(account_data)
      END_SERIALIZE()
    };
    void assign_account(const currency::account_base& acc);
    void generate(const std::wstring& wallet, const std::string& password);
    void restore(const std::wstring& path, const std::string& pass, const std::string& restore_key);
    void load(const std::wstring& wallet, const std::string& password);    
    void store();
    void store(const std::wstring& path);
    std::wstring get_wallet_path(){ return m_wallet_file; }
    currency::account_base& get_account(){return m_account;}

    void get_recent_transfers_history(std::vector<wallet_rpc::wallet_transfer_info>& trs, size_t offset, size_t count);
    uint64_t get_recent_transfers_total_count();
    void get_unconfirmed_transfers(std::vector<wallet_rpc::wallet_transfer_info>& trs);
    void init(const std::string& daemon_address = "http://localhost:8080");
    bool deinit();

    void stop() { m_stop.store(true, std::memory_order_relaxed); }
    void reset_creation_time(uint64_t timestamp);

    //i_wallet2_callback* callback() const { return m_wcallback; }
    //void callback(i_wallet2_callback* callback) { m_callback = callback; }
    void callback(std::shared_ptr<i_wallet2_callback> callback) { m_wcallback = callback; m_do_rise_transfer = true; }
    void set_do_rise_transfer(bool do_rise) { m_do_rise_transfer = do_rise; }

    bool has_related_alias_entry_unconfirmed(const currency::transaction& tx);
    void scan_tx_pool(bool& has_related_alias_in_unconfirmed);
    void refresh();
    void refresh(size_t & blocks_fetched);
    void refresh(size_t & blocks_fetched, bool& received_money, std::atomic<bool>& stop);
    bool refresh(size_t & blocks_fetched, bool& received_money, bool& ok, std::atomic<bool>& stop);
    void refresh(std::atomic<bool>& stop);
    
    void resend_unconfirmed();
    void push_offer(const bc_services::offer_details_ex& od, currency::transaction& res_tx);
    void cancel_offer_by_id(const crypto::hash& tx_id, uint64_t of_ind, currency::transaction& tx);
    void update_offer_by_id(const crypto::hash& tx_id, uint64_t of_ind, const bc_services::offer_details_ex& od, currency::transaction& res_tx);
    void request_alias_registration(const currency::extra_alias_entry& ai, currency::transaction& res_tx, uint64_t fee, uint64_t reward);
    void request_alias_update(currency::extra_alias_entry& ai, currency::transaction& res_tx, uint64_t fee, uint64_t reward);
    bool check_available_sources(std::list<uint64_t>& amounts);
    

    bool set_core_proxy(const std::shared_ptr<i_core_proxy>& proxy);
    std::shared_ptr<i_core_proxy> get_core_proxy();
    uint64_t balance() const;
    uint64_t balance(uint64_t& unloked, uint64_t& awaiting_in, uint64_t& awaiting_out, uint64_t& mined) const;
    uint64_t balance(uint64_t& unloked) const;
    uint64_t unlocked_balance() const;

    void transfer(uint64_t amount, const currency::account_public_address& acc);

    template<typename T>
    void transfer(const std::vector<currency::tx_destination_entry>& dsts,
                  size_t fake_outputs_count, 
                  uint64_t unlock_time, 
                  uint64_t fee, 
                  const std::vector<currency::extra_v>& extra, 
                  const std::vector<currency::attachment_v> attachments, 
                  T destination_split_strategy, 
                  const tx_dust_policy& dust_policy);

    template<typename destination_split_strategy_t>
    void transfer(const std::vector<currency::tx_destination_entry>& dsts,
      size_t fake_outputs_count,
      uint64_t unlock_time,
      uint64_t fee,
      const std::vector<currency::extra_v>& extra,
      const std::vector<currency::attachment_v> attachments,
      destination_split_strategy_t destination_split_strategy,
      const tx_dust_policy& dust_policy,
      currency::transaction &tx,
      uint8_t tx_outs_attr = CURRENCY_TO_KEY_OUT_RELAXED,
      bool shuffle = true,
      uint8_t flags = 0,
      bool send_to_network = true);

    void transfer(const std::vector<currency::tx_destination_entry>& dsts, 
                  size_t fake_outputs_count, 
                  uint64_t unlock_time, 
                  uint64_t fee, 
                  const std::vector<currency::extra_v>& extra, 
                  const std::vector<currency::attachment_v> attachments);
    
    void transfer(const std::vector<currency::tx_destination_entry>& dsts, 
                  size_t fake_outputs_count, 
                  uint64_t unlock_time, 
                  uint64_t fee, 
                  const std::vector<currency::extra_v>& extra, 
                  const std::vector<currency::attachment_v> attachments, 
                  currency::transaction& tx);

    template<typename destination_split_strategy_t>
    void transfer_from_contract(
      const std::list<currency::account_keys>& owner_keys,
      crypto::hash multisig_id,
      const std::vector<currency::tx_destination_entry>& dsts,
      size_t fake_outputs_count,
      uint64_t unlock_time,
      uint64_t fee,
      const std::vector<currency::extra_v>& extra,
      const std::vector<currency::attachment_v> attachments,
      destination_split_strategy_t destination_split_strategy,
      const tx_dust_policy& dust_policy,
      currency::transaction &tx,
      uint8_t tx_outs_attr,
      bool shuffle,
      uint8_t flags,
      bool send_to_network);


    void build_escrow_release_templates(crypto::hash multisig_id,
      uint64_t fee,
      currency::transaction& tx_release_template,
      currency::transaction& tx_burn_template,
      const bc_services::contract_private_details& ecrow_details);

    void build_escrow_cancel_template(crypto::hash multisig_id,
      uint64_t expiration_period,
      currency::transaction& tx_cancel_template,
      const bc_services::contract_private_details& ecrow_details);



    void build_escrow_template(const bc_services::contract_private_details& ecrow_detaild,
      size_t fake_outputs_count,
      uint64_t unlock_time,
      uint64_t expiration_time,
      uint64_t b_release_fee,
      const std::string& payment_id,
      currency::transaction& tx, 
      std::vector<uint64_t>& selected_transfers, 
      currency::keypair& one_time_key);

    void send_escrow_proposal(const bc_services::contract_private_details& ecrow_detaild,
      size_t fake_outputs_count,
      uint64_t unlock_time,
      uint64_t expiration_period,
      uint64_t fee,
      uint64_t b_release_fee,
      const std::string& payment_id,
      currency::transaction &proposal_tx,
      currency::transaction &escrow_template_tx /*,
      crypto::hash& contract_id*/ );

    bool check_connection();
    template<typename idle_condition_cb_t> //do refresh as external callback
    static bool scan_pos(mining_context& cxt, std::atomic<bool>& stop, idle_condition_cb_t idle_condition_cb, const currency::core_runtime_config &runtime_config);
    bool fill_mining_context(mining_context& ctx);
    void get_transfers(wallet2::transfer_container& incoming_transfers) const;
    
    // Returns all payments by given id in unspecified order
    void get_payments(const std::string& payment_id, std::list<payment_details>& payments) const;
    
    bool get_transfer_address(const std::string& adr_str, currency::account_public_address& addr, std::string& payment_id);
    uint64_t get_blockchain_current_height() const { return m_blockchain.size(); }
    template <class t_archive>
    inline void serialize(t_archive &a, const unsigned int ver)
    {
      if (ver < WALLET_FILE_SERIALIZATION_VERSION)
        return;
      a & m_blockchain;
      a & m_transfers;
      a & m_multisig_transfers;
      a & m_key_images;
      a & m_unconfirmed_txs;
      a & m_unconfirmed_multisig_transfers;
      a & m_payments;
      a & m_transfer_history;
      a & m_unconfirmed_in_transfers;
      a & m_contracts;
      a & m_money_expirations;
    }
    bool is_transfer_ready_to_go(const transfer_details& td, uint64_t fake_outputs_count);
    bool is_transfer_able_to_go(const transfer_details& td, uint64_t fake_outputs_count);
    uint64_t select_indices_for_transfer(std::vector<uint64_t>& ind, free_amounts_cache_type& found_free_amounts, uint64_t needed_money, uint64_t fake_outputs_count);

    //PoS
    //synchronous version of function 
    bool try_mint_pos();
    //for unit tests
    friend class test_generator;
    
    //next functions in public area only because of test_generator
    //TODO: Need refactoring - remove it back to private zone 
    void set_genesis(const crypto::hash& genesis_hash);
    bool prepare_and_sign_pos_block(currency::block& b,
      const currency::pos_entry& pos_info,
      const crypto::public_key& source_tx_pub_key,
      uint64_t in_tx_output_index,
      const std::vector<const crypto::public_key*>& keys_ptrs);
    void process_new_blockchain_entry(const currency::block& b, 
      const currency::block_direct_data_entry& bche, 
      const crypto::hash& bl_id,
      uint64_t height);
    bool get_pos_entries(currency::COMMAND_RPC_SCAN_POS::request& req);
    bool build_minted_block(const currency::COMMAND_RPC_SCAN_POS::request& req, const currency::COMMAND_RPC_SCAN_POS::response& rsp, uint64_t new_block_expected_height = UINT64_MAX);
    bool build_minted_block(const currency::COMMAND_RPC_SCAN_POS::request& req, const currency::COMMAND_RPC_SCAN_POS::response& rsp, const currency::account_public_address& miner_address, uint64_t new_block_expected_height = UINT64_MAX);
    bool reset_history();
    bool is_transfer_unlocked(const transfer_details& td) const;
    void get_mining_history(wallet_rpc::mining_history& hist);
    void set_core_runtime_config(const currency::core_runtime_config& pc);  
    currency::core_runtime_config& get_core_runtime_config();
    bool backup_keys(const std::string& path);
    bool reset_password(const std::string& pass);
    bool is_password_valid(const std::string& pass);
    bool get_actual_offers(std::list<bc_services::offer_details_ex>& offers, bool fake = false);
    bool get_fake_offers(std::list<bc_services::offer_details_ex>& offers, uint64_t amount);
    bool process_contract_info(wallet_rpc::wallet_transfer_info& wti, const std::vector<currency::payload_items_v>& decrypted_attach);
    bool handle_proposal(wallet_rpc::wallet_transfer_info& wti, const bc_services::proposal_body& prop);
    void accept_proposal(const crypto::hash& contract_id, uint64_t b_acceptance_fee, currency::transaction* p_acceptance_tx = nullptr);
    void finish_contract(const crypto::hash& contract_id, const std::string& release_type, currency::transaction* p_release_tx = nullptr);
    void request_cancel_contract(const crypto::hash& contract_id, uint64_t fee, uint64_t expiration_period, currency::transaction* p_cancellation_proposal_tx = nullptr);
    void accept_cancel_contract(const crypto::hash& contract_id, currency::transaction* p_cancellation_acceptance_tx = nullptr);

    void scan_tx_to_key_inputs(std::vector<uint64_t>& found_transfers, const currency::transaction& tx);
    void dump_trunsfers(std::stringstream& ss, bool verbose = true) const;
    std::string dump_trunsfers(bool verbose = false) const;
    void dump_key_images(std::stringstream& ss);
    void get_multisig_transfers(multisig_transfer_container& ms_transfers);
    const multisig_transfer_container& get_multisig_transfers() const { return m_multisig_transfers; }
    void set_miner_text_info(const std::string& mti) { m_miner_text_info = mti; }
    
    bool get_transfer_info_by_key_image(const crypto::key_image& ki, transfer_details& td, size_t& i);
    bool get_transfer_info_by_index(size_t i, transfer_details& td);
    size_t scan_for_collisions(std::unordered_map<crypto::key_image, std::list<size_t> >& key_images);
    size_t fix_collisions();
    size_t scan_for_transaction_entries(const crypto::hash& tx_id, const crypto::key_image& ki, std::list<transfer_details>& details);

    bool get_contracts(escrow_contracts_container& contracts);
    const std::list<expiration_entry_info>& get_expiration_entries() const { return m_money_expirations; };

    template<typename destination_split_strategy_t>
    void prepare_transaction(const std::vector<currency::tx_destination_entry>& dsts,
      size_t fake_outputs_count,
      uint64_t unlock_time,
      uint64_t fee,
      const std::vector<currency::extra_v>& extra,
      const std::vector<currency::attachment_v> attachments,
      destination_split_strategy_t destination_split_strategy,
      const tx_dust_policy& dust_policy,
      const currency::account_public_address& crypt_address,
      currency::transaction &tx,
      uint8_t tx_outs_attr,
      bool shuffle,
      bool mark_tx_as_complete,
      uint8_t flags,
      std::vector<uint64_t>& selected_transfers,
      currency::keypair& one_time_key,
      std::vector<currency::tx_destination_entry>& prepared_destinations,
      crypto::hash multisig_id = currency::null_hash);

private:
    void add_transfers_to_expiration_list(const std::vector<uint64_t>& selected_transfers, uint64_t expiration, uint64_t change_amount, const crypto::hash& related_tx_id);
    void remove_transfer_from_expiration_list(uint64_t transfer_index);
    bool store_keys(std::string& buff, const std::string& password);
    void load_keys(const std::string& keys_file_name, const std::string& password);
    void process_new_transaction(const currency::transaction& tx, uint64_t height, const currency::block& b);
    void detach_blockchain(uint64_t height);
    void get_short_chain_history(std::list<crypto::hash>& ids);
    bool extract_offers_from_transfer_entry(size_t i, std::unordered_map<crypto::hash, bc_services::offer_details_ex>& offers_local);
    bool select_my_offers(std::list<bc_services::offer_details_ex>& offers);
    bool clear();
    bool reset_all();
    bool on_idle();
    void unserialize_block_complete_entry(const currency::COMMAND_RPC_GET_BLOCKS_FAST::response& serialized,
      currency::COMMAND_RPC_GET_BLOCKS_DIRECT::response& unserialized);
    void pull_blocks(size_t& blocks_added, std::atomic<bool>& stop);
    bool prepare_free_transfers_cache(uint64_t fake_outputs_count);
    uint64_t select_transfers(uint64_t needed_money, size_t fake_outputs_count, uint64_t dust, std::vector<uint64_t>& selected_indicies);
    void add_transfers_to_transfers_cache(const std::vector<uint64_t>& indexs);
    void add_transfer_to_transfers_cache(uint64_t amount, uint64_t index);
    bool prepare_file_names(const std::string& file_path);
    void process_unconfirmed(const currency::transaction& tx, std::vector<std::string>& recipients, std::vector<std::string>& recipients_aliases);
    void add_sent_unconfirmed_tx(const currency::transaction& tx,
                                 const std::vector<std::string>& recipients, 
                                 const std::vector<uint64_t>& selected_indicies, 
                                 const std::vector<currency::tx_destination_entry>& splitted_dsts);
    bool read_money_transfer2_details_from_tx(const currency::transaction& tx,
                                                        const std::vector<currency::tx_destination_entry>& splitted_dsts, 
                                                        wallet_rpc::wallet_transfer_info_details& wtd);

    void update_current_tx_limit();
    void prepare_wti(wallet_rpc::wallet_transfer_info& wti, uint64_t height, uint64_t timestamp, const currency::transaction& tx, uint64_t amount, const money_transfer2_details& td);
    void prepare_wti_decrypted_attachments(wallet_rpc::wallet_transfer_info& wti, const std::vector<currency::payload_items_v>& decrypted_att);
    void handle_money_received2(const currency::block& b,
                                const currency::transaction& tx, 
                                uint64_t amount, 
                                const money_transfer2_details& td);
    void handle_money_spent2(const currency::block& b,  
                             const currency::transaction& in_tx, 
                             uint64_t amount, 
                             const money_transfer2_details& td, 
                             const std::vector<std::string>& recipients,
                             const std::vector<std::string>& recipients_aliases);
    void handle_pulled_blocks(size_t& blocks_added, std::atomic<bool>& stop,
      currency::COMMAND_RPC_GET_BLOCKS_DIRECT::response& blocks);
    std::string get_alias_for_address(const std::string& addr);
    static bool build_kernel(const currency::pos_entry& pe, const currency::stake_modifier_type& stake_modifier, currency::stake_kernel& kernel, uint64_t& coindays_weight, uint64_t timestamp);
    bool is_connected_to_net();
    bool is_transfer_okay_for_pos(const transfer_details& tr);
    bool scan_unconfirmed_outdate_tx();
    const currency::transaction& get_transaction_by_id(const crypto::hash& tx_hash);
    void rise_on_transfer2(const wallet_rpc::wallet_transfer_info& wti);
    void process_genesis_if_needed(const currency::block& genesis);
    bool build_escrow_proposal(bc_services::contract_private_details& ecrow_details, uint64_t fee, uint64_t unlock_time, currency::tx_service_attachment& att, std::vector<uint64_t>& selected_indicies);
    bool prepare_tx_sources(uint64_t needed_money, size_t fake_outputs_count, uint64_t dust_threshold, std::vector<currency::tx_source_entry>& sources, std::vector<uint64_t>& selected_indicies, uint64_t& found_money);
    bool prepare_tx_sources(crypto::hash multisig_id, std::vector<currency::tx_source_entry>& sources, uint64_t& found_money);
    uint64_t get_needed_money(uint64_t fee, const std::vector<currency::tx_destination_entry>& dsts);
    template<class destination_split_strategy_t>
    void prepare_tx_destinations(uint64_t needed_money,
      uint64_t found_money,
      destination_split_strategy_t destination_split_strategy,
      const tx_dust_policy& dust_policy,
      const std::vector<currency::tx_destination_entry>& dsts,
      std::vector<currency::tx_destination_entry>& final_detinations);
    bool handle_contract(wallet_rpc::wallet_transfer_info& wti, const bc_services::contract_private_details& cntr, const std::vector<currency::payload_items_v>& decrypted_attach);
    bool handle_release_contract(wallet_rpc::wallet_transfer_info& wti, const std::string& release_instruction);
    bool handle_cancel_proposal(wallet_rpc::wallet_transfer_info& wti, const bc_services::escrow_cancel_templates_body& ectb, const std::vector<currency::payload_items_v>& decrypted_attach);
    bool handle_expiration_list(uint64_t tx_expiration_ts_median);
    void handle_contract_expirations(uint64_t tx_expiration_ts_median);

    uint64_t get_tx_expiration_median() const;

    void print_tx_sent_message(const currency::transaction& tx, const std::string& description, uint64_t fee);

    // Validates escrow template tx in assumption it's related to wallet's account (wallet's account is either A or B party in escrow process)
    bool validate_escrow_proposal(const wallet_rpc::wallet_transfer_info& wti, const bc_services::proposal_body& prop,
      std::vector<currency::payload_items_v>& decrypted_items, crypto::hash& ms_id, bc_services::contract_private_details& cpd);

    bool validate_escrow_contract(const wallet_rpc::wallet_transfer_info& wti, const bc_services::contract_private_details& cpd, bool is_a,
      const std::vector<currency::payload_items_v>& decrypted_items, crypto::hash& ms_id, bc_services::escrow_relese_templates_body& rtb);

    bool validate_escrow_cancel_proposal(const wallet_rpc::wallet_transfer_info& wti, const bc_services::escrow_cancel_templates_body& ectb,
      const std::vector<currency::payload_items_v>& decrypted_items, crypto::hash& ms_id, bc_services::contract_private_details& cpd,
      const currency::transaction& proposal_template_tx);

    struct construct_tx_param
    {
      std::vector<currency::tx_destination_entry> dsts;
      size_t fake_outputs_count;
      uint64_t unlock_time;
      uint64_t fee;
      std::vector<currency::extra_v> extra;
      std::vector<currency::attachment_v> attachments;
      tx_dust_policy dust_policy;
      currency::account_public_address crypt_address;      
      uint8_t tx_outs_attr;
      bool shuffle;
      bool mark_tx_as_complete;
      uint8_t flags;
      crypto::hash multisig_id; 
    };

    struct constructed_tx_data
    {
      std::vector<currency::tx_destination_entry> prepared_destinations;
      currency::transaction tx;
      std::vector<uint64_t> selected_transfers;
      currency::keypair one_time_key;
    };


    template<typename destination_split_strategy_t>
    void prepare_transaction(const construct_tx_param& construct_tx_data,
      constructed_tx_data& res, destination_split_strategy_t destination_split_strategy);


    void send_transaction_to_network(const currency::transaction& tx);
    void add_sent_tx_detailed_info(const currency::transaction& tx,
      const std::vector<currency::tx_destination_entry>& destinations,
      const std::vector<uint64_t>& selected_indicies);
    void mark_transfers_as_spent(const std::vector<uint64_t>& selected_transfers, const std::string& reason = std::string());
    void mark_transfers_with_flag(const std::vector<uint64_t>& selected_transfers, uint32_t flag, const std::string& reason = std::string());
    void clear_transfers_from_flag(const std::vector<uint64_t>& selected_transfers, uint32_t flag, const std::string& reason = std::string());
    void exception_handler();
    uint64_t get_minimum_allowed_fee_for_contract(const crypto::hash& ms_id);




    currency::account_base m_account;
    std::wstring m_wallet_file;
    std::string m_password;
    std::vector<crypto::hash> m_blockchain;
    std::atomic<uint64_t> m_local_bc_height; //temporary workaround 
    std::atomic<uint64_t> m_last_bc_timestamp; 
    bool m_do_rise_transfer;

    transfer_container m_transfers;
    multisig_transfer_container m_multisig_transfers;
    payment_container m_payments;
    std::unordered_map<crypto::key_image, size_t> m_key_images;
    uint64_t m_upper_transaction_size_limit; //TODO: auto-calc this value or request from daemon, now use some fixed value

    std::atomic<bool> m_stop;
    std::vector<wallet_rpc::wallet_transfer_info> m_transfer_history;
    std::unordered_map<crypto::hash, currency::transaction> m_unconfirmed_in_transfers;
    std::unordered_map<crypto::hash, tools::wallet_rpc::wallet_transfer_info> m_unconfirmed_txs;
    std::unordered_set<crypto::hash> m_unconfirmed_multisig_transfers;

    std::shared_ptr<i_core_proxy> m_core_proxy;
    std::shared_ptr<i_wallet2_callback> m_wcallback;
    uint64_t m_height_of_start_sync;
    uint64_t m_last_sync_percent;
    currency::core_runtime_config m_core_runtime_config;
    escrow_contracts_container m_contracts;
    std::list<expiration_entry_info> m_money_expirations;
    //optimization for big wallets and batch tx

    free_amounts_cache_type m_found_free_amounts;
    uint64_t m_fake_outputs_count;
    std::string m_miner_text_info;

 
  };

}


BOOST_CLASS_VERSION(tools::wallet2, WALLET_FILE_SERIALIZATION_VERSION)
BOOST_CLASS_VERSION(tools::wallet_rpc::wallet_transfer_info, 9)
BOOST_CLASS_VERSION(tools::wallet2::transfer_details, 2)


namespace boost
{
  namespace serialization
  {
    template <class Archive>
    inline void serialize(Archive &a, tools::wallet2::transaction_wallet_info &x, const boost::serialization::version_type ver)
    {
      a & x.m_block_height;
      a & x.m_block_timestamp;
      a & x.m_tx;
    }

    template <class Archive>
    inline void serialize(Archive &a, tools::wallet2::transfer_details_base &x, const boost::serialization::version_type ver)
    {
      a & x.m_ptx_wallet_info;
      a & x.m_internal_output_index;
      a & x.m_flags;
      a & x.m_spent_height;
    }

    template <class Archive>
    inline void serialize(Archive &a, tools::wallet2::transfer_details &x, const boost::serialization::version_type ver)
    {
      a & x.m_global_output_index;
      a & x.m_key_image;
      a & static_cast<tools::wallet2::transfer_details_base&>(x);
    }



    template <class Archive>
    inline void serialize(Archive& a, tools::wallet2::payment_details& x, const boost::serialization::version_type ver)
    {
      a & x.m_tx_hash;
      a & x.m_amount;
      a & x.m_block_height;
      a & x.m_unlock_time;
    }
    
    template <class Archive>
    inline void serialize(Archive& a, tools::wallet_rpc::wallet_transfer_info_details& x, const boost::serialization::version_type ver)
    {
      a & x.rcv;
      a & x.spn;
    }

    template <class Archive>
    inline void serialize(Archive& a, tools::wallet_rpc::wallet_transfer_info& x, const boost::serialization::version_type ver)
    {      

      a & x.amount;
      a & x.timestamp;
      a & x.tx_hash;
      a & x.height;
      a & x.tx_blob_size;
      a & x.payment_id;
      a & x.remote_addresses; 
      a & x.is_income;
      a & x.td;
      a & x.tx;
      a & x.recipients_aliases;
      a & x.comment;
      a & x.contract;
      a & x.selected_indicies;
      a & x.srv_attachments;
      //do not store this items in the file since it's quite easy to restore it from original tx 
      if (Archive::is_loading::value)
      {
        x.unlock_time = currency::get_tx_unlock_time(x.tx);        
        x.is_service = currency::is_service_tx(x.tx);
        x.is_mixing = currency::is_mixin_tx(x.tx);
        x.is_mining = currency::is_coinbase(x.tx);
        if (!x.is_mining)
          x.fee = currency::get_tx_fee(x.tx);
        else
          x.fee = 0;
        x.show_sender = currency::is_showing_sender_addres(x.tx);
        x.tx_type = get_tx_type(x.tx);
      }
    }

    template <class Archive>
    inline void serialize(Archive& a, tools::wallet_rpc::escrow_contract_details_basic& x, const boost::serialization::version_type ver)
    {
      a & x.state;
      a & x.is_a;
      a & x.private_detailes;
      a & x.expiration_time;
      a & x.cancel_expiration_time;
      a & x.timestamp;
      a & x.height;
      a & x.payment_id;
      //is not kv_serialization map
      a & x.proposal;
      a & x.release_body;
      a & x.cancel_body;
    }

    template <class Archive>
    inline void serialize(Archive& a, tools::wallet_rpc::escrow_contract_details& x, const boost::serialization::version_type ver)
    {
      a & static_cast<tools::wallet_rpc::escrow_contract_details_basic&>(x);
      a & x.contract_id;
    }


    template <class Archive>
    inline void serialize(Archive& a, tools::wallet2::expiration_entry_info& x, const boost::serialization::version_type ver)
    {
      a & x.expiration_time;
      a & x.change_amount;
      a & x.selected_transfers;
      a & x.related_tx_id;
    }

    


  }
}

namespace tools
{
  namespace detail
  {



    //----------------------------------------------------------------------------------------------------
    inline void digit_split_strategy(const std::vector<currency::tx_destination_entry>& dsts,
      const currency::tx_destination_entry& change_dst, uint64_t dust_threshold,
      std::vector<currency::tx_destination_entry>& splitted_dsts, uint64_t& dust, uint64_t max_output_allowed)
    {
      splitted_dsts.clear();
      dust = 0;

      BOOST_FOREACH(auto& de, dsts)
      {
        if (de.addr.size() > 1)
        {
          //for multisig we don't split
          splitted_dsts.push_back(de);
        }
        else
        {
          currency::decompose_amount_into_digits(de.amount, dust_threshold,
            [&](uint64_t chunk) { splitted_dsts.push_back(currency::tx_destination_entry(chunk, de.addr)); },
            [&](uint64_t a_dust) { splitted_dsts.push_back(currency::tx_destination_entry(a_dust, de.addr)); }, max_output_allowed);
        }
      }

      if (change_dst.amount > 0)
      {
        if (change_dst.addr.size() > 1)
        {
          //for multisig we don't split
          splitted_dsts.push_back(change_dst);
        }
        else
        {
          currency::decompose_amount_into_digits(change_dst.amount, dust_threshold,
            [&](uint64_t chunk) { splitted_dsts.push_back(currency::tx_destination_entry(chunk, change_dst.addr)); },
            [&](uint64_t a_dust) { dust = a_dust; }, max_output_allowed);

        }
      }
    }
    //----------------------------------------------------------------------------------------------------

    inline void null_split_strategy(const std::vector<currency::tx_destination_entry>& dsts,
      const currency::tx_destination_entry& change_dst, uint64_t dust_threshold,
      std::vector<currency::tx_destination_entry>& splitted_dsts, uint64_t& dust, uint64_t max_output_allowed)
    {
      splitted_dsts = dsts;

      dust = 0;
      uint64_t change = change_dst.amount;
      if (0 < dust_threshold)
      {
        for (uint64_t order = 10; order <= 10 * dust_threshold; order *= 10)
        {
          uint64_t dust_candidate = change_dst.amount % order;
          uint64_t change_candidate = (change_dst.amount / order) * order;
          if (dust_candidate <= dust_threshold)
          {
            dust = dust_candidate;
            change = change_candidate;
          }
          else
          {
            break;
          }
        }
      }

      if (0 != change)
      {
        splitted_dsts.push_back(currency::tx_destination_entry(change, change_dst.addr));
      }
    }
    //----------------------------------------------------------------------------------------------------
    inline void void_split_strategy(const std::vector<currency::tx_destination_entry>& dsts,
      const currency::tx_destination_entry& change_dst, uint64_t dust_threshold,
      std::vector<currency::tx_destination_entry>& splitted_dsts, uint64_t& dust, uint64_t max_output_allowed)
    {
      splitted_dsts = dsts;
      if (change_dst.amount > 0)
        splitted_dsts.push_back(change_dst);
    }
    //----------------------------------------------------------------------------------------------------
    inline void print_source_entry(const currency::tx_source_entry& src)
    {
      std::ostringstream indexes;
      std::for_each(src.outputs.begin(), src.outputs.end(), [&](const currency::tx_source_entry::output_entry& s_e) { indexes << s_e.first << " "; });
      LOG_PRINT_L0("amount=" << currency::print_money(src.amount) << ", real_output=" <<src.real_output << ", real_output_in_tx_index=" << src.real_output_in_tx_index << ", indexes: " << indexes.str());
    }
    //----------------------------------------------------------------------------------------------------
  }
  //----------------------------------------------------------------------------------------------------
  template<typename T>
  void wallet2::transfer(const std::vector<currency::tx_destination_entry>& dsts, size_t fake_outputs_count,
    uint64_t unlock_time, uint64_t fee, const std::vector<currency::extra_v>& extra, const std::vector<currency::attachment_v> attachments, T destination_split_strategy, const tx_dust_policy& dust_policy)
  {
    currency::transaction tx;
    transfer(dsts, fake_outputs_count, unlock_time, fee, extra, attachments, destination_split_strategy, dust_policy, tx);
  }
  //----------------------------------------------------------------------------------------------------
  template<typename destination_split_strategy_t>
  void wallet2::prepare_tx_destinations(uint64_t needed_money,
    uint64_t found_money,
    destination_split_strategy_t destination_split_strategy,
    const tx_dust_policy& dust_policy,
    const std::vector<currency::tx_destination_entry>& dsts,
    std::vector<currency::tx_destination_entry>& final_detinations)
  {
    currency::tx_destination_entry change_dts = AUTO_VAL_INIT(change_dts);
    if (needed_money < found_money)
    {
      change_dts.addr.push_back(m_account.get_keys().m_account_address);
      change_dts.amount = found_money - needed_money;
    }
    THROW_IF_FALSE_WALLET_EX(found_money >= needed_money, error::wallet_internal_error, "needed_money(" + std::to_string(needed_money)
      + ") < found_money(" + std::to_string(found_money) + ") ");

    uint64_t dust = 0;
    destination_split_strategy(dsts, change_dts, dust_policy.dust_threshold, final_detinations, dust, WALLET_MAX_ALLOWED_OUTPUT_AMOUNT);
    THROW_IF_FALSE_WALLET_EX(dust_policy.dust_threshold >= dust, error::wallet_internal_error, "invalid dust value: dust = " +
      std::to_string(dust) + ", dust_threshold = " + std::to_string(dust_policy.dust_threshold));
    
    //@#@
#ifdef _DEBUG
    if (final_detinations.size() > 10)
    {
      LOG_PRINT_L0("final_detinations.size()=" << final_detinations.size());
    }
#endif
    //@#@

    if (0 != dust && !dust_policy.add_to_fee)
    {
      final_detinations.push_back(currency::tx_destination_entry(dust, dust_policy.addr_for_dust));
    }
  }

  //----------------------------------------------------------------------------------------------------
  template<typename destination_split_strategy_t>
  void wallet2::prepare_transaction(const std::vector<currency::tx_destination_entry>& dsts,
    size_t fake_outputs_count,
    uint64_t unlock_time,
    uint64_t fee,
    const std::vector<currency::extra_v>& extra,
    const std::vector<currency::attachment_v> attachments,
    destination_split_strategy_t destination_split_strategy,
    const tx_dust_policy& dust_policy,
    const currency::account_public_address& crypt_address,
    currency::transaction &tx,
    uint8_t tx_outs_attr,
    bool shuffle,
    bool mark_tx_as_complete,
    uint8_t flags,
    std::vector<uint64_t>& selected_transfers,
    currency::keypair& one_time_key,
    std::vector<currency::tx_destination_entry>& prepared_destinations,
    crypto::hash multisig_id)
  {
    TIME_MEASURE_START_MS(get_needed_money_time);
    uint64_t needed_money = get_needed_money(fee, dsts);
    if (flags&TX_FLAG_SIGNATURE_MODE_SEPARATE && tx.vout.size())
    {      
      needed_money += (currency::get_outs_money_amount(tx) - get_inputs_money_amount(tx));
    }
    TIME_MEASURE_FINISH_MS(get_needed_money_time);

    std::vector<currency::tx_source_entry> sources;
    uint64_t found_money = 0;

    TIME_MEASURE_START_MS(prepare_tx_sources_time);
    if (multisig_id == currency::null_hash)
    {
      prepare_tx_sources(needed_money, fake_outputs_count, dust_policy.dust_threshold, sources, selected_transfers, found_money);
    }
    else
    {
      prepare_tx_sources(multisig_id, sources, found_money);
    }
    TIME_MEASURE_FINISH_MS(prepare_tx_sources_time);

    TIME_MEASURE_START_MS(prepare_tx_destinations_time);
    prepare_tx_destinations(needed_money, found_money, destination_split_strategy, dust_policy, dsts, prepared_destinations);
    TIME_MEASURE_FINISH_MS(prepare_tx_destinations_time);

    if (mark_tx_as_complete && !sources.empty())
      sources.back().separately_signed_tx_complete = true;

    TIME_MEASURE_START_MS(construct_tx_time);
    bool r = currency::construct_tx(m_account.get_keys(),
      sources,
      prepared_destinations,
      extra,
      attachments,
      tx,
      one_time_key.sec,
      unlock_time,
      crypt_address, 
      0,
      tx_outs_attr,
      shuffle,
      flags);
    TIME_MEASURE_FINISH_MS(construct_tx_time);
    THROW_IF_TRUE_WALLET_EX(!r, error::tx_not_constructed, sources, prepared_destinations, unlock_time);

    TIME_MEASURE_START_MS(sign_ms_input_time);
    if (multisig_id != currency::null_hash)
    {
        // In case there's multisig input is used -- sign it partially with this wallet's keys (we don't have any others here).
        // NOTE: this tx will not be ready to send until all other necessary signs for ms input would made.
        auto it = m_multisig_transfers.find(multisig_id);
        THROW_IF_FALSE_WALLET_INT_ERR_EX(it != m_multisig_transfers.end(), "can't find multisig_id: " << multisig_id);
        const currency::transaction& ms_source_tx = it->second.m_ptx_wallet_info->m_tx;
        bool is_tx_input_fully_signed = false;
        r = sign_multisig_input_in_tx(tx, 0, m_account.get_keys(), ms_source_tx, &is_tx_input_fully_signed);
        THROW_IF_FALSE_WALLET_INT_ERR_EX(r && !is_tx_input_fully_signed, "sign_multisig_input_in_tx failed: r = " << r << ", is_tx_input_fully_signed = " << is_tx_input_fully_signed);
    }
    TIME_MEASURE_FINISH_MS(sign_ms_input_time);

    THROW_IF_TRUE_WALLET_EX(CURRENCY_MAX_TRANSACTION_BLOB_SIZE <= get_object_blobsize(tx), error::tx_too_big, tx, m_upper_transaction_size_limit);
    LOG_PRINT_GREEN("[prepare_transaction]: get_needed_money_time: " << get_needed_money_time << " ms"
      << ", prepare_tx_sources_time: " << prepare_tx_sources_time << " ms"
      << ", prepare_tx_destinations_time: " << prepare_tx_destinations_time << " ms"
      << ", construct_tx_time: " << construct_tx_time << " ms"
      << ", sign_ms_input_time: " << sign_ms_input_time << " ms",
      LOG_LEVEL_0);
  }

  template<typename destination_split_strategy_t>
  void wallet2::prepare_transaction(const construct_tx_param& construct_tx_data, constructed_tx_data& res, 
    destination_split_strategy_t destination_split_strategy)
  {
    return prepare_transaction(construct_tx_data.dsts,
      construct_tx_data.fake_outputs_count,
      construct_tx_data.unlock_time,
      construct_tx_data.fee,
      construct_tx_data.extra,
      construct_tx_data.attachments,
      destination_split_strategy,
      construct_tx_data.dust_policy,
      construct_tx_data.crypt_address,
      res.tx,
      construct_tx_data.tx_outs_attr,
      construct_tx_data.shuffle,
      construct_tx_data.mark_tx_as_complete,
      construct_tx_data.flags,
      res.selected_transfers,
      res.one_time_key,
      res.prepared_destinations,
      construct_tx_data.multisig_id);
  }


  //----------------------------------------------------------------------------------------------------
  template<typename destination_split_strategy_t>
  void wallet2::transfer(const std::vector<currency::tx_destination_entry>& dsts, 
                         size_t fake_outputs_count,
                         uint64_t unlock_time, 
                         uint64_t fee, 
                         const std::vector<currency::extra_v>& extra, 
                         const std::vector<currency::attachment_v> attachments, 
                         destination_split_strategy_t destination_split_strategy,
                         const tx_dust_policy& dust_policy, 
                         currency::transaction &tx,
                         uint8_t tx_outs_attr, 
                         bool shuffle, 
                         uint8_t flags, 
                         bool send_to_network)
  {
    TIME_MEASURE_START(precalculation_time);
    using namespace currency;
    std::vector<uint64_t> selected_transfers;
    std::vector<currency::tx_destination_entry> prepared_destinations;
    account_public_address crypt_address = currency::get_crypt_address_from_destinations(m_account.get_keys(), dsts);
    currency::keypair onetime_keys = AUTO_VAL_INIT(onetime_keys);
    TIME_MEASURE_FINISH(precalculation_time);

    TIME_MEASURE_START(prepare_transaction_time);
    prepare_transaction(dsts,
      fake_outputs_count,
      unlock_time,
      fee,
      extra,
      attachments,
      destination_split_strategy,
      dust_policy,
      crypt_address,
      tx,
      tx_outs_attr,
      shuffle,
      false,
      flags,
      selected_transfers, 
      onetime_keys,
      prepared_destinations);
    TIME_MEASURE_FINISH(prepare_transaction_time);

    TIME_MEASURE_START(send_transaction_to_network_time);
    if (send_to_network)
      send_transaction_to_network(tx);
    TIME_MEASURE_FINISH(send_transaction_to_network_time);

    TIME_MEASURE_START(mark_transfers_as_spent_time);
    mark_transfers_as_spent(selected_transfers, std::string("money transfer"));
    TIME_MEASURE_FINISH(mark_transfers_as_spent_time);

    TIME_MEASURE_START(add_sent_tx_detailed_info_time);
    add_sent_tx_detailed_info(tx, prepared_destinations, selected_transfers);
    TIME_MEASURE_FINISH(add_sent_tx_detailed_info_time);

    LOG_PRINT_GREEN("[wallet::transfer] prepare_transaction_time: " << print_fixed_decimal_point(prepare_transaction_time, 3)
      << ", precalculation_time: " << print_fixed_decimal_point(precalculation_time, 3)
      << ", send_transaction_to_network_time: " << print_fixed_decimal_point(send_transaction_to_network_time, 3)
      << ", mark_transfers_as_spent_time: " << print_fixed_decimal_point(mark_transfers_as_spent_time, 3)
      << ", add_sent_tx_detailed_info_time: " << print_fixed_decimal_point(add_sent_tx_detailed_info_time, 3),
      LOG_LEVEL_0);
    
    //print_tx_sent_message(tx, std::string() + "(transaction)", fee);
  }
  //----------------------------------------------------------------------------------------------------
  template<typename idle_condition_cb_t> //do refresh as external callback
  bool wallet2::scan_pos(mining_context& cxt,
    std::atomic<bool>& stop,
    idle_condition_cb_t idle_condition_cb,
    const currency::core_runtime_config &runtime_config)
  {
    cxt.rsp.status = CORE_RPC_STATUS_NOT_FOUND;
    uint64_t timstamp_start = runtime_config.get_core_time();
    uint64_t timstamp_last_idle_call = runtime_config.get_core_time();


    for (size_t i = 0; i != cxt.sp.pos_entries.size(); i++)
    {
      //set timestamp starting from timestamp%POS_SCAN_STEP = 0
      uint64_t adjusted_starter_timestamp = timstamp_start - POS_SCAN_STEP;
      adjusted_starter_timestamp = POS_SCAN_STEP * 2 - (adjusted_starter_timestamp%POS_SCAN_STEP) + adjusted_starter_timestamp;
      
      bool go_past = true;
      uint64_t step = 0;
      
      auto next_turn = [&](){
        if (!step)
        {
          step += POS_SCAN_STEP;
        }
        else if (go_past)
        {
          go_past = false;
        }
        else
        {
          go_past = true;
          step += POS_SCAN_STEP;
        }
      };

      while(step <= POS_SCAN_WINDOW)
      {

        //check every WALLET_POS_MINT_CHECK_HEIGHT_INTERVAL seconds if top block changes, in case - break loop 
        if (runtime_config.get_core_time() - timstamp_last_idle_call > WALLET_POS_MINT_CHECK_HEIGHT_INTERVAL)
        {
          if (!idle_condition_cb())
          {
            LOG_PRINT_L0("Detected new block, minting interrupted");
            cxt.rsp.status = CORE_RPC_STATUS_NOT_FOUND;
            return false;
          }
          timstamp_last_idle_call = runtime_config.get_core_time();
        }


        uint64_t ts = go_past ? adjusted_starter_timestamp - step : adjusted_starter_timestamp + step;
        if (ts < cxt.rsp.starter_timestamp)
        {
          next_turn();
          continue;
        }
        PROFILE_FUNC("general_mining_iteration");
        if (stop)
          return false;
        currency::stake_kernel sk = AUTO_VAL_INIT(sk);
        uint64_t coindays_weight = 0;
        build_kernel(cxt.sp.pos_entries[i], cxt.sm, sk, coindays_weight, ts);
        crypto::hash kernel_hash;
        {
          PROFILE_FUNC("calc_hash");
          kernel_hash = crypto::cn_fast_hash(&sk, sizeof(sk));
        }

        currency::wide_difficulty_type this_coin_diff = cxt.basic_diff / coindays_weight;
        bool check_hash_res;
        {
          PROFILE_FUNC("check_hash");
          check_hash_res = currency::check_hash(kernel_hash, this_coin_diff);
        }
        if (check_hash_res)
        {
          //found kernel
          LOG_PRINT_GREEN("Found kernel: amount=" << currency::print_money(cxt.sp.pos_entries[i].amount) << ENDL
            << "difficulty_basic=" << cxt.basic_diff << ", diff for this coin: " << this_coin_diff << ENDL
            << "index=" << cxt.sp.pos_entries[i].index << ENDL
            << "kernel info: " << ENDL
            << print_stake_kernel_info(sk) << ENDL 
            << "kernel_hash(proof): " << kernel_hash,
            LOG_LEVEL_0);
          cxt.rsp.index = i;
          cxt.rsp.block_timestamp = ts;
          cxt.rsp.status = CORE_RPC_STATUS_OK;
          return true;
        }
        
        next_turn();
        
        
      }
    }
    cxt.rsp.status = CORE_RPC_STATUS_NOT_FOUND;
    return false;
  }


}

#undef LOG_DEFAULT_CHANNEL 
#define LOG_DEFAULT_CHANNEL "wallet"
ENABLE_CHANNEL_BY_DEFAULT("wallet");