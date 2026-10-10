// The native library template (template-library/): its TurboModule
// ("Example") and native component (RNGtkExampleView, a GtkCalendar),
// which rn-gtk-host registers as an app's autolinked library would be.
// rn-gtk-host --module GalleryNativeModule --self-test calls the module,
// sets the component's props, picks a day in the calendar and sends it a
// command.
import React, {useEffect, useRef, useState} from 'react';
import {Animated, Easing, Pressable, StyleSheet, Text, View} from 'react-native';
import AnimatedProbeNativeComponent from './AnimatedProbeNativeComponent';
import {Example, ExampleView, ExampleViewCommands} from '../../template-library/src';

const AnimatedProbe = Animated.createAnimatedComponent(AnimatedProbeNativeComponent);

// A library component's own prop on the native driver: rn-gtk-host's probe
// records the values its update() gets.
function ProbeAnimation() {
  const value = useRef(new Animated.Value(0)).current;
  const [running, setRunning] = useState(false);
  return (
    <View style={styles.row}>
      <Action
        id="probe-animate"
        label="animate the probe (native driver)"
        onPress={() => {
          setRunning(true);
          value.setValue(0);
          Animated.timing(value, {toValue: 1, duration: 1500, easing: Easing.linear, useNativeDriver: true}).start(
            () => setRunning(false),
          );
        }}
      />
      <Text style={styles.status}>{running ? 'probe animating' : 'probe idle'}</Text>
      <AnimatedProbe nativeID="probe" value={value} style={styles.probe} />
    </View>
  );
}

function Action({id, label, onPress}) {
  return (
    <Pressable nativeID={id} onPress={onPress} style={styles.action}>
      <Text style={styles.actionText}>{label}</Text>
    </Pressable>
  );
}

export default function GalleryNativeModule() {
  const [greeting] = useState(() => Example?.greet('GTK') ?? 'no module');
  const [version, setVersion] = useState('-');
  const [described] = useState(() => JSON.stringify(Example?.describe({a: 1, b: [2, 3]})));
  const [date, setDate] = useState('2026-10-09');
  const [weeks, setWeeks] = useState(false);
  const [picked, setPicked] = useState('-');
  const calendar = useRef(null);
  useEffect(() => {
    Example?.gtkVersion().then(setVersion);
  }, []);
  return (
    <View style={styles.root}>
      <Text style={styles.status}>
        greet: {greeting} · gtkVersion: {version} · describe: {described}
      </Text>
      <Text style={styles.status}>
        date prop {date} · picked {picked} · weeks {String(weeks)}
      </Text>
      <View style={styles.row}>
        <Action id="set-date" label="date = 2027-01-15" onPress={() => setDate('2027-01-15')} />
        <Action id="weeks" label="showWeekNumbers" onPress={() => setWeeks(w => !w)} />
        <Action id="today" label="Commands.showToday" onPress={() => ExampleViewCommands.showToday(calendar.current)} />
      </View>
      <ProbeAnimation />
      <ExampleView
        ref={calendar}
        nativeID="calendar"
        date={date}
        showWeekNumbers={weeks}
        onDateChange={e => setPicked(e.nativeEvent.date)}
        style={styles.calendar}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 16, gap: 10, backgroundColor: '#FFFFFF'},
  status: {fontSize: 14, color: '#1C1C1E'},
  row: {flexDirection: 'row', gap: 8, alignItems: 'center'},
  probe: {width: 24, height: 24},
  action: {paddingVertical: 8, paddingHorizontal: 12, borderRadius: 6, backgroundColor: '#3584E4'},
  actionText: {color: '#FFFFFF', fontSize: 14},
  calendar: {width: 320, height: 300},
});
