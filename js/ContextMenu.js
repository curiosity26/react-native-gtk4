/**
 * A View with a context menu: right-click, the Menu key or Shift+F10 pops
 * it up (a GtkPopoverMenu). Any View takes the same `contextMenu` prop;
 * this adds an `onSelect` for all items.
 *
 *   <ContextMenu
 *     items={[
 *       {title: 'Open', shortcut: 'Ctrl+O', onSelect: open},
 *       {type: 'separator'},
 *       {title: 'Show hidden files', checked: showHidden, onSelect: toggle},
 *       {title: 'Sort by', items: [
 *         {title: 'Name', type: 'radio', checked: sort === 'name'},
 *         {title: 'Date', type: 'radio', checked: sort === 'date'},
 *       ]},
 *       {title: 'Delete', disabled: !selected},
 *     ]}
 *     onSelect={item => console.log(item.title)}>
 *     ...
 *   </ContextMenu>
 *
 * A TextInput or selectable Text inside keeps its own menu (GTK's, or
 * Copy / Select All) unless it has a contextMenu of its own.
 */
import * as React from 'react';
import {View} from 'react-native';

function withOnSelect(items, onSelect) {
  if (onSelect == null || !Array.isArray(items)) {
    return items;
  }
  return items.map(item => {
    if (item == null || item === '-' || item.type === 'separator') {
      return item;
    }
    const submenu = item.items ?? item.submenu;
    return {
      ...item,
      items: submenu != null ? withOnSelect(submenu, onSelect) : undefined,
      onSelect: chosen => {
        item.onSelect?.(chosen);
        onSelect(chosen);
      },
    };
  });
}

export default function ContextMenu({items, onSelect, ref, ...props}) {
  return <View {...props} ref={ref} contextMenu={withOnSelect(items, onSelect)} />;
}
