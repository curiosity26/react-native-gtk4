/**
 * Menu items, as the app writes them, to what the host's menus take
 * (linux/src/GtkMenus.cc): functions stay here, and each item gets an id
 * from its place ('2', '1/0' for a submenu's first item), by which a choice
 * comes back.
 *
 *   {title, onSelect, disabled, checked, type: 'checkbox' | 'radio',
 *    shortcut: 'Ctrl+S', items: [...a submenu]}
 *   {type: 'separator'}  (or the string '-')
 *
 * `checked` alone makes a checkbox. Radio items between two separators are
 * one group.
 */

export function toNativeMenu(items) {
  const byId = new Map();
  const convert = (list, prefix) =>
    (Array.isArray(list) ? list : [])
      .filter(item => item != null && item !== false)
      .map((item, i) => {
        if (item === '-' || item.type === 'separator') {
          return {type: 'separator'};
        }
        const id = prefix + i;
        const type =
          item.type === 'checkbox' || item.type === 'radio'
            ? item.type
            : item.checked != null
              ? 'checkbox'
              : 'item';
        const node = {
          id,
          title: String(item.title ?? ''),
          type,
          disabled: item.disabled === true || item.enabled === false,
          checked: item.checked === true,
          shortcut: item.shortcut ?? '',
        };
        const submenu = item.items ?? item.submenu;
        if (submenu != null) {
          node.items = convert(submenu, id + '/');
        }
        byId.set(id, item);
        return node;
      });
  const native = convert(items, '');
  return {
    items: native,
    // Calls the item's onSelect; returns the item (or undefined).
    select(id) {
      const item = byId.get(id);
      if (item != null && typeof item.onSelect === 'function') {
        item.onSelect(item);
      }
      return item;
    },
  };
}

// A view's contextMenu prop as native props: the items, and the event
// that runs the chosen item's onSelect, then the view's own
// onContextMenuSelect({nativeEvent: {id}}).
export function contextMenuProps(contextMenu, onContextMenuSelect) {
  if (contextMenu == null) {
    return null;
  }
  const menu = toNativeMenu(contextMenu);
  return {
    contextMenu: menu.items,
    onContextMenuSelect: event => {
      menu.select(event.nativeEvent.id);
      onContextMenuSelect?.(event);
    },
  };
}
