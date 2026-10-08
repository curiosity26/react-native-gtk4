import React from 'react';
import {StyleSheet, Text, View} from 'react-native';

export default function App() {
  return (
    <View style={styles.root}>
      <View style={styles.card}>
        <Text style={styles.title}>Hello, World!</Text>
        <Text style={styles.subtitle}>React Native on GTK4</Text>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {
    flex: 1,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: '#F5F5F7',
  },
  card: {
    width: 400,
    height: 200,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: '#FFFFFF',
    borderRadius: 16,
    borderWidth: 2,
    borderColor: '#007AFF',
  },
  title: {
    fontSize: 36,
    fontWeight: 'bold',
    color: '#1C1C1E',
  },
  subtitle: {
    marginTop: 8,
    fontSize: 16,
    color: '#6E6E73',
  },
});
