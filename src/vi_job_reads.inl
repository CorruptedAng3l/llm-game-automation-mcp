// Counter observations are scoped to this native job's finite execution window.
// Poll returns and exact-key subclass dispatch are separate observations, never
// proof that application code consumed input or produced a gameplay action.
vi_input_reads::Sample JobReadSample(int vk){
 const auto& t=g_keyTrace[vk];vi_input_reads::Sample out;
 const ChannelTrace* channels[]={&t.gaks,&t.gks,&t.gkbs,&t.di_state,&t.di_buffer,&t.raw_data,&t.raw_buffer};
 for(unsigned i=0;i<vi_input_reads::channels;++i)if(!channels[i]->snapshot(out.reads[i],out.down[i]))out.channelValid[i]=false;
 out.messages=t.delivered_down.load();return out;
}
std::array<int,2> JobReadAliases(int vk){
 const int families[][3]={{VK_SHIFT,VK_LSHIFT,VK_RSHIFT},{VK_CONTROL,VK_LCONTROL,VK_RCONTROL},{VK_MENU,VK_LMENU,VK_RMENU}};
 for(const auto& family:families)if(vk==family[0]||vk==family[1]||vk==family[2]){std::array<int,2> aliases{};unsigned n=0;for(int member:family)if(member!=vk)aliases[n++]=member;return aliases;}
 return {};
}
void JobReadsStart(InputJob& job,ULONGLONG now){
 auto& observation=job.readObservation;observation.started=true;observation.pad=job.pad;observation.began=now;
 if(!job.pad&&job.kind!=InputJob::Placement)for(int key:job.keys)if(key>0&&key<256&&std::none_of(observation.keys.begin(),observation.keys.end(),[&](const vi_input_reads::Key& old){return old.vk==key;})){
  vi_input_reads::Key entry;entry.vk=key;entry.before=JobReadSample(key);const auto aliases=JobReadAliases(key);
  for(unsigned n=0;n<aliases.size();++n)if(aliases[n]){entry.aliases[n].vk=aliases[n];entry.aliases[n].before=JobReadSample(aliases[n]);}
  observation.keys.push_back(entry);
 }
}
void JobReadsSample(InputJob& job,bool freeze=false){
 auto& observation=job.readObservation;if(!observation.started||observation.frozen)return;
 if(job.pad){std::lock_guard<std::mutex> pad(g_padStateMx);if(g_padReadOwner==job.id){observation.padReturns=g_padReadReturns;observation.valid=observation.valid&&g_padReadValid;}}
 else for(auto& key:observation.keys){key.delta=vi_input_reads::Difference(key.before,JobReadSample(key.vk));
  for(auto& alias:key.aliases)if(alias.vk)alias.delta=vi_input_reads::Difference(alias.before,JobReadSample(alias.vk));}
 if(freeze){observation.ended=HousekeepingNow();observation.frozen=true;}
}
void JobReadFields(JOut& out,const InputJob& job){
 const auto& observation=job.readObservation;JOut reads;reads.num("version",2);reads.bol("started",observation.started);reads.bol("terminal",job.done);reads.bol("frozen",observation.frozen);
 reads.str("kind",job.pad?"pad":"keys");reads.raw("application_response","null");
 reads.num("window_ms",observation.started?(double)((observation.frozen?observation.ended:HousekeepingNow())-observation.began):0);
 bool valid=observation.valid,pollingValid=observation.valid,messagesValid=true;std::uint64_t padReturns=observation.padReturns;if(job.pad&&!observation.frozen){std::lock_guard<std::mutex> pad(g_padStateMx);if(g_padReadOwner==job.id){padReturns=g_padReadReturns;valid=valid&&g_padReadValid;}}unsigned observed=0,pollObserved=0,messageObserved=0;std::string keys="[";
 const char* names[]={"GetAsyncKeyState","GetKeyState","GetKeyboardState","DirectInputState","DirectInputBuffer","GetRawInputData","GetRawInputBuffer","WindowMessageDispatch"};bool named[8]{};
 if(!job.pad)for(const auto& key:observation.keys){const auto delta=observation.frozen?key.delta:vi_input_reads::Difference(key.before,JobReadSample(key.vk));std::array<vi_input_reads::Delta,2> aliases{};
  for(unsigned n=0;n<aliases.size();++n)if(key.aliases[n].vk)aliases[n]=observation.frozen?key.aliases[n].delta:vi_input_reads::Difference(key.aliases[n].before,JobReadSample(key.aliases[n].vk));
  const auto evidence=vi_input_reads::KeyEvidence(delta,aliases);valid=valid&&evidence.measured;pollingValid=pollingValid&&evidence.pollMeasured;messagesValid=messagesValid&&evidence.messageMeasured;
  if(evidence.observed)++observed;if(evidence.pollObserved)++pollObserved;if(evidence.messageObserved)++messageObserved;
  if(keys.size()>1)keys+=",";JOut item;item.num("vk",key.vk);item.bol("measured",evidence.measured);item.bol("polling_measured",evidence.pollMeasured);item.bol("messages_measured",evidence.messageMeasured);
  if(delta.pollValid){item.str("poll_reads",std::to_string(delta.reads));item.str("down_returns",std::to_string(delta.down));}else{item.raw("poll_reads","null");item.raw("down_returns","null");}
  if(delta.messageValid)item.str("message_dispatches",std::to_string(delta.messages));else item.raw("message_dispatches","null");
  JOut channels;std::string observedChannels="[";
  for(unsigned n=0;n<vi_input_reads::channels;++n){JOut channel;channel.bol("measured",delta.channelValid[n]);if(delta.channelValid[n]){channel.str("reads",std::to_string(delta.channelReads[n]));channel.str("down_returns",std::to_string(delta.channelDown[n]));if(delta.channelDown[n]){if(observedChannels.size()>1)observedChannels+=",";observedChannels+="\""+std::string(names[n])+"\"";named[n]=true;}}else{channel.raw("reads","null");channel.raw("down_returns","null");}channels.raw(names[n],channel.done());}
  if(evidence.messageObserved){if(observedChannels.size()>1)observedChannels+=",";observedChannels+="\"WindowMessageDispatch\"";named[7]=true;}
  item.raw("channels",channels.done());item.raw("observed_channels",observedChannels+"]");item.str("delivery_observation",!evidence.measured?"unavailable":evidence.pollObserved?(evidence.messageObserved?"poll_high_and_message_dispatch_observed":"poll_high_observed"):evidence.messageObserved?"message_dispatch_observed":"no_message_or_poll_high_observed");
  if(key.aliases[0].vk){item.bol("exact_vk_measured",delta.pollValid);item.bol("modifier_alias_ambiguous",evidence.aliasAmbiguous);std::string related="[";
   for(unsigned n=0;n<aliases.size();++n){if(n)related+=",";JOut alias;alias.num("vk",key.aliases[n].vk);alias.bol("measured",aliases[n].pollValid);if(aliases[n].pollValid){alias.str("poll_reads",std::to_string(aliases[n].reads));alias.str("down_returns",std::to_string(aliases[n].down));}else{alias.raw("poll_reads","null");alias.raw("down_returns","null");}related+=alias.done();}item.raw("modifier_aliases",related+"]");}
  keys+=item.done();}
 keys+="]";reads.raw("keys",keys);reads.str("pad_returns",std::to_string(padReturns));
 const bool applicable=job.pad||!observation.keys.empty();const bool measured=observation.started&&valid&&applicable;
 reads.bol("measured",measured);reads.bol("applicable",applicable);
 if(measured)reads.bol("all_requested_observed",job.pad?padReturns>0:observed==observation.keys.size());else reads.raw("all_requested_observed","null");
 reads.num("keys_observed",observed);reads.num("keys_requested",(double)observation.keys.size());
 reads.num("keys_with_poll_high",pollObserved);reads.num("keys_with_message_dispatch",messageObserved);
 reads.str("outcome",!observation.started?"pending":!applicable?"not_applicable":!valid?"unavailable":!job.done?"pending":(job.pad?padReturns>0:observed==observation.keys.size())?"observed":observed?"partial":"not_observed");
 if(job.pad)pollingValid=valid;const bool pollingMeasured=observation.started&&pollingValid&&applicable;
 reads.bol("polling_measured",pollingMeasured);if(pollingMeasured)reads.bol("all_requested_polling_observed",job.pad?padReturns>0:pollObserved==observation.keys.size());else reads.raw("all_requested_polling_observed","null");
 reads.str("polling_outcome",!observation.started?"pending":!applicable?"not_applicable":!pollingValid?"unavailable":!job.done?"pending":(job.pad?padReturns>0:pollObserved==observation.keys.size())?"observed":pollObserved?"partial":"not_observed");
 if(!job.pad&&observation.started&&messagesValid&&applicable)reads.bol("all_requested_messages_dispatched",messageObserved==observation.keys.size());else reads.raw("all_requested_messages_dispatched","null");
 std::string observedChannels="[";if(job.pad&&valid&&padReturns)observedChannels+="\"XInput\"";else if(!job.pad)for(unsigned n=0;n<8;++n)if(named[n]){if(observedChannels.size()>1)observedChannels+=",";observedChannels+="\""+std::string(names[n])+"\"";}reads.raw("observed_channels",observedChannels+"]");
 reads.str("scope","exact-key polling high-state returns or vinput-subclass message dispatch between native job start and cleanup; message dispatch does not prove application consumption; polling-only facts remain separate; related modifier alias highs without an exact high are ambiguous; pad returns match this job's publication owner; no counter proves causality or gameplay response and no input is retried");out.raw("input_read_observation",reads.done());
}
