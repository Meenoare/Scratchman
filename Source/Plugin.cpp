// SPDX-License-Identifier: AGPL-3.0-or-later
#include <JuceHeader.h>
#include "Engine.h"
using namespace juce;
class Processor : public AudioProcessor {
public:
 AudioProcessorValueTreeState state;
 std::atomic<double> playhead{-1};
 String filename="No audio loaded";
 Processor():AudioProcessor(BusesProperties().withOutput("Output",AudioChannelSet::stereo(),true)),state(*this,nullptr,"ScratchLab",layout()){formats.registerBasicFormats();}
 static AudioProcessorValueTreeState::ParameterLayout layout(){
  AudioProcessorValueTreeState::ParameterLayout p;
  auto add=[&](const char* id,const char* label,float lo,float hi,float step,float def){p.add(std::make_unique<AudioParameterFloat>(ParameterID{id,1},label,NormalisableRange<float>(lo,hi,step),def));};
  add("tempo","Tempo",40,200,1,100);add("beats","Gesture beats",.25f,4,.25f,1);add("speed","Speed",.25f,3,.01f,1);add("stroke","Source span ms",10,1500,1,250);add("variation","Variation percent",0,60,1,12);add("gain","Output",0,1,.001f,.7f);
  p.add(std::make_unique<AudioParameterBool>(ParameterID{"sync",1},"Host BPM",true));p.add(std::make_unique<AudioParameterBool>(ParameterID{"repeat",1},"Repeat while held",false));p.add(std::make_unique<AudioParameterInt>(ParameterID{"slice",1},"Selected slice",0,31,0));return p;
 }
 float value(const char* id)const{return state.getRawParameterValue(id)->load();}
 std::shared_ptr<const scratch::Clip> getClip()const{return std::atomic_load(&clip);}
 // Keep replaced clips alive until processor destruction: no sample-buffer deallocation on audio thread.
 void publish(std::shared_ptr<const scratch::Clip> c){std::lock_guard<std::mutex> guard(historyMutex);auto old=getClip();if(old)history.push_back(std::move(old));std::atomic_store(&clip,std::move(c));}
 int selected()const{return int(value("slice"));}
 void select(int n){auto c=getClip();n=jlimit(0,c?int(c->cuts.size())-2:31,n);auto* p=state.getParameter("slice");p->setValueNotifyingHost(p->convertTo0to1(float(n)));}
 void command(int n){int a,k,b,m;fifo.prepareToWrite(1,a,k,b,m);if(k){queue[size_t(a)]=n;fifo.finishedWrite(1);}}
 void prepareToPlay(double sr,int)override{engine.prepare(sr);gain.reset(sr,.01);gain.setCurrentAndTargetValue(value("gain"));playhead=-1;}
 void releaseResources()override{engine.kill();playhead=-1;}
 bool isBusesLayoutSupported(const BusesLayout& b)const override{return b.getMainInputChannelSet().isDisabled()&&b.getMainOutputChannelSet()==AudioChannelSet::stereo();}
 void start(int n,int note=-1){scratch::Settings s;s.bpm=value("sync")>.5?bpm:value("tempo");s.beats=value("beats");s.speed=value("speed");s.strokeMs=value("stroke");s.variation=value("variation")/100;s.repeat=value("repeat")>.5;engine.trigger(getClip(),selected(),n<0?-1:n,s,n==-2);activeNote=note;}
 void handle(const MidiMessage& msg){
  if(msg.isAllNotesOff()||msg.isAllSoundOff()){engine.stop();return;}
  if(msg.isNoteOn()){int n=msg.getNoteNumber();if(n>=36&&n<56)start(n-36,n);else if(n==60||n==61)start(n==60?-1:-2,n);else if(n>=72&&n<104)select(n-72);}
  if(msg.isNoteOff()&&msg.getNoteNumber()==activeNote&&engine.isRepeating())engine.stop();
 }
 void processBlock(AudioBuffer<float>& b,MidiBuffer& midi)override{
  ScopedNoDenormals nd;b.clear();if(pendingReset.exchange(false))engine.kill();if(auto* host=getPlayHead())if(auto pos=host->getPosition())if(auto v=pos->getBpm())bpm=jlimit(40.,300.,*v);
  int a,k,z,m;fifo.prepareToRead(128,a,k,z,m);for(int i=0;i<k+m;++i){int cmd=queue[size_t(i<k?a+i:z+i-k)];if(cmd==-3)engine.stop();else if(cmd==-4){if(activeNote==-1&&engine.isRepeating())engine.stop();}else start(cmd);}fifo.finishedRead(k+m);
  gain.setTargetValue(value("gain"));auto event=midi.begin();auto end=midi.end();auto* l=b.getWritePointer(0);auto* r=b.getWritePointer(1);
  for(int i=0;i<b.getNumSamples();++i){while(event!=end&&(*event).samplePosition<=i){handle((*event).getMessage());++event;}float x,y;engine.tick(x,y);float g=gain.getNextValue();l[i]=x*g;r[i]=y*g;}midi.clear();playhead=engine.position();
 }
 String load(const File& file){
  std::unique_ptr<AudioFormatReader> reader(formats.createReaderFor(file));if(!reader)return "Cannot decode file. Try WAV, AIFF or FLAC.";
  if(reader->sampleRate<8000||reader->sampleRate>192000||reader->lengthInSamples<32||reader->lengthInSamples>reader->sampleRate*60||reader->numChannels<1)return "Use audio up to 60 seconds / 192 kHz, at least 32 frames.";
  AudioBuffer<float> data(2,int(reader->lengthInSamples));if(!reader->read(&data,0,data.getNumSamples(),0,true,true))return "Audio decoding failed.";if(reader->numChannels==1)data.copyFrom(1,0,data,0,0,data.getNumSamples());
  auto audio=std::make_shared<scratch::Audio>();audio->rate=reader->sampleRate;for(int c=0;c<2;++c){audio->ch[size_t(c)].assign(data.getReadPointer(c),data.getReadPointer(c)+data.getNumSamples());for(float x:audio->ch[size_t(c)])if(!std::isfinite(x))return "Audio contains invalid samples.";}auto next=std::make_shared<scratch::Clip>();next->audio=audio;for(int i=0;i<=8;++i)next->cuts.push_back(double(data.getNumSamples())/audio->rate*i/8);publish(next);filename=file.getFileName();select(0);command(-3);return {};
 }
 void demo(){auto audio=std::make_shared<scratch::Audio>();audio->rate=44100;for(auto& channel:audio->ch)channel.resize(132300);for(int i=0;i<132300;++i){double t=i/audio->rate,q=scratch::frac(t/.375),env=std::sin(scratch::pi*std::min(1.,q*4))*std::exp(-q*3);float v=float(env*(.4*std::sin(2*scratch::pi*(170*t+1.3*std::sin(2*scratch::pi*8*t)))+.2*std::sin(2*scratch::pi*710*t)));audio->ch[0][size_t(i)]=audio->ch[1][size_t(i)]=v;}auto c=std::make_shared<scratch::Clip>();c->audio=audio;for(int i=0;i<=8;++i)c->cuts.push_back(3.*i/8);publish(c);filename="Generated demo";select(0);command(-3);}
 void slice(int count,bool transient){auto old=getClip();if(!old)return;auto c=std::make_shared<scratch::Clip>();c->audio=old->audio;double dur=double(c->audio->ch[0].size())/c->audio->rate;count=jlimit(1,32,count);
  if(!transient){for(int i=0;i<=count;++i)c->cuts.push_back(dur*i/count);}else{
   const auto& a=*c->audio;const size_t hop=size_t(std::max(1,int(a.rate*.01)));std::vector<std::pair<double,double>> peaks;double previous=0;for(size_t i=0;i<a.ch[0].size();i+=hop){double sum=0;size_t end=std::min(i+hop,a.ch[0].size());for(size_t j=i;j<end;++j)sum+=a.ch[0][j]*a.ch[0][j];double rms=std::sqrt(sum/double(end-i));peaks.push_back({std::max(0.,rms-previous),double(i)/a.rate});previous=rms;}std::sort(peaks.rbegin(),peaks.rend());c->cuts={0,dur};double threshold=peaks.empty()?1:std::max(.00001,peaks.front().first*.12),gap=std::min(.1,dur/count*.4);for(auto peak:peaks){if(int(c->cuts.size())>=count+1||peak.first<threshold)break;bool ok=true;for(double t:c->cuts)if(std::abs(peak.second-t)<gap)ok=false;if(ok)c->cuts.push_back(peak.second);}std::sort(c->cuts.begin(),c->cuts.end());
  }publish(c);select(0);command(-3);
 }
 void boundary(int i,double seconds){auto old=getClip();if(!old||i<=0||i>=int(old->cuts.size())-1||!std::isfinite(seconds))return;auto c=std::make_shared<scratch::Clip>(*old);double gap=1/c->audio->rate,lo=c->cuts[size_t(i-1)]+gap,hi=c->cuts[size_t(i+1)]-gap;if(lo>hi)return;c->cuts[size_t(i)]=jlimit(lo,hi,seconds);publish(c);command(-3);}
 void split(double seconds){auto old=getClip();if(!old||old->cuts.size()>=33||seconds<=0||seconds>=old->cuts.back())return;for(double x:old->cuts)if(std::abs(x-seconds)<.005)return;auto c=std::make_shared<scratch::Clip>(*old);c->cuts.push_back(seconds);std::sort(c->cuts.begin(),c->cuts.end());publish(c);command(-3);}
 void merge(){auto old=getClip();int i=selected();if(!old||i<0||i>=int(old->cuts.size())-2)return;auto c=std::make_shared<scratch::Clip>(*old);c->cuts.erase(c->cuts.begin()+i+1);publish(c);command(-3);}
 const String getName()const override{return "Scratch Lab";}bool acceptsMidi()const override{return true;}bool producesMidi()const override{return false;}bool hasEditor()const override{return true;}
 double getTailLengthSeconds()const override{return .01;}int getNumPrograms()override{return 1;}int getCurrentProgram()override{return 0;}void setCurrentProgram(int)override{}const String getProgramName(int)override{return {};}void changeProgramName(int,const String&)override{}
 AudioProcessorEditor* createEditor()override;
 void getStateInformation(MemoryBlock& dest)override{
  auto tree=state.copyState();tree.setProperty("filename",filename,nullptr);MemoryBlock params;copyXmlToBinary(*tree.createXml(),params);MemoryOutputStream out(dest,false);out.writeInt(0x534C4233);out.writeInt(int(params.getSize()));out.write(params.getData(),params.getSize());auto c=getClip();out.writeBool(bool(c));if(!c)return;out.writeDouble(c->audio->rate);out.writeInt(int(c->audio->ch[0].size()));for(const auto& channel:c->audio->ch)for(float f:channel)out.writeFloat(f);out.writeInt(int(c->cuts.size()));for(double t:c->cuts)out.writeDouble(t);
 }
 void setStateInformation(const void* data,int bytes)override{
  if(bytes<13)return;MemoryInputStream in(data,size_t(bytes),false);if(in.readInt()!=0x534C4233)return;int length=in.readInt();if(length<1||length>in.getNumBytesRemaining()-1)return;MemoryBlock params;in.readIntoMemoryBlock(params,size_t(length));auto xml=getXmlFromBinary(params.getData(),int(params.getSize()));if(!xml)return;auto tree=ValueTree::fromXml(*xml);if(!tree.hasType("ScratchLab"))return;std::shared_ptr<scratch::Clip> c;
  if(in.readBool()){double rate=in.readDouble();int frames=in.readInt();if(!std::isfinite(rate)||rate<8000||rate>192000||frames<1||frames>rate*60||int64(frames)*8+4>in.getNumBytesRemaining())return;auto a=std::make_shared<scratch::Audio>();a->rate=rate;for(auto& channel:a->ch){channel.resize(size_t(frames));for(float& x:channel){x=in.readFloat();if(!std::isfinite(x))return;}}c=std::make_shared<scratch::Clip>();c->audio=a;int count=in.readInt();if(count<2||count>33||int64(count)*8>in.getNumBytesRemaining())return;for(int i=0;i<count;++i){double t=in.readDouble();if(!std::isfinite(t)||t<0||t>double(frames)/rate+.000001||(i&&t<=c->cuts.back()))return;c->cuts.push_back(t);}if(c->cuts.front()!=0||std::abs(c->cuts.back()-double(frames)/rate)>.00001)return;}
  state.replaceState(tree);filename=tree.getProperty("filename","Restored sample").toString();publish(c);pendingReset=true;
 }
private:
 std::shared_ptr<const scratch::Clip> clip;std::vector<std::shared_ptr<const scratch::Clip>> history;std::mutex historyMutex;AudioFormatManager formats;scratch::Engine engine;SmoothedValue<float> gain;double bpm=100;int activeNote=-1;AbstractFifo fifo{128};std::array<int,128> queue{};std::atomic<bool> pendingReset{false};
};

class Editor final : public AudioProcessorEditor, private Timer, public FileDragAndDropTarget {
 struct Pad : TextButton {
  std::function<void()> down,up;
  void mouseDown(const MouseEvent& e) override {TextButton::mouseDown(e);if(down)down();}
  void mouseUp(const MouseEvent& e) override {TextButton::mouseUp(e);if(up)up();}
 };
 struct Wave : Component {
  Processor& p;std::shared_ptr<const scratch::Clip> cached;
  std::vector<std::pair<float,float>> peaks;
  explicit Wave(Processor& proc):p(proc){}
  void paint(Graphics& g) override {
   g.fillAll(Colour(0xff101713));auto c=p.getClip();
   if(!c){g.setColour(Colours::grey);g.drawText("DROP AUDIO HERE",getLocalBounds(),Justification::centred);return;}
   if(c!=cached){cached=c;peaks.clear();auto& a=c->audio->ch[0];size_t hop=std::max(size_t(1),a.size()/1500);
    for(size_t i=0;i<a.size();i+=hop){float lo=0,hi=0;for(size_t j=i;j<std::min(a.size(),i+hop);++j){lo=std::min(lo,a[j]);hi=std::max(hi,a[j]);}peaks.emplace_back(lo,hi);}}
   float w=float(getWidth()),h=float(getHeight());double duration=c->cuts.back();int s=jlimit(0,int(c->cuts.size())-2,p.selected());
   g.setColour(Colour(0xff304226));g.fillRect(float(c->cuts[size_t(s)]/duration)*w,0.f,float((c->cuts[size_t(s+1)]-c->cuts[size_t(s)])/duration)*w,h);
   g.setColour(Colour(0xffb9d690));for(size_t i=0;i<peaks.size();++i)g.drawVerticalLine(int(double(i)/double(peaks.size())*w),h*.5f+peaks[i].first*h*.46f,h*.5f+peaks[i].second*h*.46f);
   g.setFont(FontOptions(10.f));for(size_t i=0;i<c->cuts.size();++i){int x=int(c->cuts[i]/duration*w);g.setColour(Colour(0xffd8fb73));g.drawVerticalLine(x,0,h);if(i+1<c->cuts.size())g.drawText(String(int(i)+1),x+4,3,30,16,Justification::centredLeft);}
   double pos=p.playhead.load();if(pos>=0){g.setColour(Colours::white);g.drawVerticalLine(int(pos/duration*w),0,h);}
  }
  void mouseDown(const MouseEvent& e) override {auto c=p.getClip();if(!c)return;double t=jlimit(0.,c->cuts.back(),double(e.x)/getWidth()*c->cuts.back());if(e.mods.isShiftDown())p.split(t);else p.select(int(std::upper_bound(c->cuts.begin(),c->cuts.end(),t)-c->cuts.begin())-1);}
 };
 Processor& p;Wave wave;LookAndFeel_V4 look;
 TextButton load{"Load audio"},demo{"Demo"},equal{"Equal slices"},transients{"Find transients"},merge{"Merge next"},stop{"STOP / Esc"};
 Pad cleanSlice,cleanPhrase;Label status;ComboBox count,selection;TextEditor startTime,endTime;
 std::array<Pad,20> pads;std::array<Slider,6> sliders;std::array<Label,6> labels;
 ToggleButton sync{"Host BPM"},repeat{"Repeat while held"};
 using SA=AudioProcessorValueTreeState::SliderAttachment;using BA=AudioProcessorValueTreeState::ButtonAttachment;
 std::array<std::unique_ptr<SA>,6> attachments;std::unique_ptr<BA> syncAttachment,repeatAttachment;
 std::unique_ptr<FileChooser> chooser;std::shared_ptr<const scratch::Clip> shown;int shownSlice=-1,lastKey=0;
 void open(const File& f){auto error=p.load(f);if(error.isNotEmpty())status.setText(error,dontSendNotification);else refresh();}
 void refresh(){
  shown=p.getClip();shownSlice=p.selected();selection.clear(dontSendNotification);
  if(shown){for(int i=0;i<int(shown->cuts.size())-1;++i)selection.addItem("Slice "+String(i+1),i+1);int s=jlimit(0,int(shown->cuts.size())-2,shownSlice);selection.setSelectedId(s+1,dontSendNotification);
   if(!startTime.hasKeyboardFocus(true))startTime.setText(String(shown->cuts[size_t(s)],6),false);
   if(!endTime.hasKeyboardFocus(true))endTime.setText(String(shown->cuts[size_t(s+1)],6),false);
   startTime.setEnabled(s>0);endTime.setEnabled(s<int(shown->cuts.size())-2);merge.setEnabled(s<int(shown->cuts.size())-2);
   status.setText(p.filename+" / "+String(shown->cuts.back(),2)+" s / "+String(int(shown->cuts.size())-1)+" slices",dontSendNotification);
  }else{status.setText("Load audio or press Demo. Drag and drop is supported.",dontSendNotification);startTime.setEnabled(false);endTime.setEnabled(false);merge.setEnabled(false);}
  for(auto& pad:pads)pad.setEnabled(bool(shown));cleanSlice.setEnabled(bool(shown));cleanPhrase.setEnabled(bool(shown));wave.repaint();
 }
 void timerCallback() override {if(shown!=p.getClip()||shownSlice!=p.selected())refresh();wave.repaint();if(lastKey&&!KeyPress::isKeyCurrentlyDown(lastKey)){p.command(-4);lastKey=0;}}
public:
 explicit Editor(Processor& proc):AudioProcessorEditor(proc),p(proc),wave(proc){
  look.setColour(TextButton::buttonColourId,Colour(0xff273138));look.setColour(TextButton::textColourOffId,Colour(0xffedf2e9));look.setColour(Slider::thumbColourId,Colour(0xffd8fb73));look.setColour(Slider::trackColourId,Colour(0xffd8fb73));look.setColour(ToggleButton::tickColourId,Colour(0xffd8fb73));setLookAndFeel(&look);
  setWantsKeyboardFocus(true);setSize(1020,810);
  for(auto* c:std::vector<Component*>{&wave,&load,&demo,&equal,&transients,&merge,&stop,&cleanSlice,&cleanPhrase,&status,&count,&selection,&startTime,&endTime,&sync,&repeat})addAndMakeVisible(c);
  status.setColour(Label::textColourId,Colour(0xffa2ae9d));status.setFont(FontOptions(12.f));
  cleanSlice.setButtonText("PLAY SLICE - CLEAN / Space");cleanPhrase.setButtonText("PLAY FULL PHRASE - CLEAN / P");
  for(auto* b:{&cleanSlice,&cleanPhrase}){b->setColour(TextButton::buttonColourId,Colour(0xff3c5229));b->down=[this,b]{p.command(b==&cleanSlice?-1:-2);};b->up=[this]{p.command(-4);};}
  load.onClick=[this]{chooser=std::make_unique<FileChooser>("Choose audio (up to 60 seconds)",File{},"*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");chooser->launchAsync(FileBrowserComponent::openMode|FileBrowserComponent::canSelectFiles,[safe=Component::SafePointer<Editor>(this)](const FileChooser& fc){if(safe&&fc.getResult().existsAsFile())safe->open(fc.getResult());});};
  demo.onClick=[this]{p.demo();refresh();};stop.onClick=[this]{p.command(-3);};
  for(int n:{4,8,16,32})count.addItem(String(n)+" slices",n);count.setSelectedId(8);
  equal.onClick=[this]{p.slice(count.getSelectedId(),false);refresh();};transients.onClick=[this]{p.slice(count.getSelectedId(),true);refresh();};merge.onClick=[this]{p.merge();refresh();};
  selection.onChange=[this]{if(selection.getSelectedId()>0)p.select(selection.getSelectedId()-1);};
  for(auto* t:{&startTime,&endTime}){t->setInputRestrictions(16,"0123456789.");t->setTextToShowWhenEmpty("Seconds",Colours::grey);}
  startTime.onReturnKey=startTime.onFocusLost=[this]{p.boundary(p.selected(),startTime.getText().getDoubleValue());refresh();};
  endTime.onReturnKey=endTime.onFocusLost=[this]{p.boundary(p.selected()+1,endTime.getText().getDoubleValue());refresh();};
  const char* keys[]={"Q","W","E","R","T","Y","U","I","O","A","S","D","F","G","H","J","K","L","Z","X"};
  for(size_t i=0;i<pads.size();++i){addAndMakeVisible(pads[i]);pads[i].setButtonText(String(keys[i])+"   "+scratch::names[i]);pads[i].down=[this,i]{p.command(int(i));};pads[i].up=[this]{p.command(-4);};pads[i].setWantsKeyboardFocus(false);}
  const char* ids[]={"tempo","beats","speed","stroke","variation","gain"};const char* titles[]={"TEMPO (BPM)","GESTURE (BEATS)","SPEED","SOURCE SPAN (ms)","VARIATION (%)","OUTPUT"};
  for(size_t i=0;i<sliders.size();++i){addAndMakeVisible(sliders[i]);sliders[i].setSliderStyle(Slider::RotaryHorizontalVerticalDrag);sliders[i].setTextBoxStyle(Slider::TextBoxBelow,false,100,22);addAndMakeVisible(labels[i]);labels[i].setText(titles[i],dontSendNotification);labels[i].setFont(FontOptions(10.f));labels[i].setJustificationType(Justification::centred);attachments[i]=std::make_unique<SA>(p.state,ids[i],sliders[i]);}
  syncAttachment=std::make_unique<BA>(p.state,"sync",sync);repeatAttachment=std::make_unique<BA>(p.state,"repeat",repeat);
  for(auto* c:std::vector<Component*>{&load,&demo,&equal,&transients,&merge,&stop,&count,&selection,&sync,&repeat,&cleanSlice,&cleanPhrase})c->setWantsKeyboardFocus(false);
  refresh();startTimerHz(25);
 }
 ~Editor()override{stopTimer();setLookAndFeel(nullptr);}
 bool isInterestedInFileDrag(const StringArray&)override{return true;}
 void filesDropped(const StringArray& f,int,int)override{if(!f.isEmpty())open(File(f[0]));}
 void paint(Graphics& g)override{
  g.fillAll(Colour(0xff111518));g.setColour(Colour(0xffd8fb73));g.setFont(FontOptions(26.f,Font::bold));g.drawText("S/ SCRATCH LAB",24,16,480,38,Justification::centredLeft);
  g.setColour(Colour(0xff9ca99b));g.setFont(FontOptions(11.f));g.drawText("NATIVE INSTRUMENT / 20 GESTURES + CLEAN PLAYBACK",510,21,484,26,Justification::centredRight);
  g.drawText("SOURCE / click to select - Shift-click to split",24,106,720,22,Justification::centredLeft);g.drawText("START (s)                          END (s)",504,288,340,20,Justification::centredLeft);g.drawText("PERFORM / buttons, keyboard or MIDI notes 36-55",24,348,750,22,Justification::centredLeft);
  g.drawText("MIDI 60 = clean slice / 61 = full phrase / 72-103 = select slice / Esc = stop",24,749,970,21,Justification::centredLeft);g.setFont(FontOptions(10.f));g.drawText("Synthetic approximations / monophonic / native-speed clean playback / Host BPM, no beat quantisation",24,774,970,18,Justification::centredLeft);
 }
 void resized()override{
  load.setBounds(24,65,130,31);demo.setBounds(164,65,85,31);status.setBounds(267,65,727,31);wave.setBounds(24,133,970,126);
  count.setBounds(24,270,116,30);equal.setBounds(150,270,123,30);transients.setBounds(283,270,140,30);merge.setBounds(433,270,125,30);selection.setBounds(24,309,170,30);startTime.setBounds(504,309,134,29);endTime.setBounds(658,309,134,29);sync.setBounds(584,268,136,30);repeat.setBounds(730,268,180,30);stop.setBounds(842,309,152,30);
  for(size_t i=0;i<pads.size();++i)pads[i].setBounds(24+int(i%5)*196,382+int(i/5)*54,186,44);
  cleanSlice.setBounds(24,604,470,39);cleanPhrase.setBounds(510,604,484,39);
  for(size_t i=0;i<sliders.size();++i){int x=24+int(i)*164;labels[i].setBounds(x,657,150,16);sliders[i].setBounds(x,675,150,69);}
 }
 bool keyPressed(const KeyPress& k)override{
  if(startTime.hasKeyboardFocus(true)||endTime.hasKeyboardFocus(true))return false;
  if(k.getKeyCode()==KeyPress::escapeKey){p.command(-3);return true;}
  auto ch=CharacterFunctions::toLowerCase(k.getTextCharacter());if(ch>='1'&&ch<='8'){p.select(int(ch-'1'));return true;}if(lastKey)return false;
  if(ch==' '||ch=='p'){p.command(ch==' '?-1:-2);lastKey=k.getKeyCode();return true;}
  int index=String("qwertyuioasdfghjklzx").indexOfChar(ch);if(index>=0){p.command(index);lastKey=k.getKeyCode();return true;}return false;
 }
 bool keyStateChanged(bool)override{if(lastKey&&!KeyPress::isKeyCurrentlyDown(lastKey)){p.command(-4);lastKey=0;return true;}return false;}
};
AudioProcessorEditor* Processor::createEditor(){return new Editor(*this);}
AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new Processor();}
